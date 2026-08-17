/*
    Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "MediaPlayerPrivateQt.h"

#include "Frame.h"
#include "FrameLoader.h"
#include "FrameView.h"
#include "GraphicsContext.h"
#include "GraphicsContextQt.h"
#include "GraphicsLayer.h"
#include "HTMLMediaElement.h"
#include "Logging.h"
#include "NetworkingContext.h"
#include "NotImplemented.h"
#include "RenderVideo.h"

#include <QBuffer>
#include <QMediaPlayerControl>
#include <QMediaService>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <limits>
#include <qmediametadata.h>
#include <qmultimedia.h>
#include <wtf/HashSet.h>
#include <wtf/text/CString.h>

#include "texmap/TextureMapper.h"

using namespace WTF;

namespace WebCore {

static constexpr qint64 seekPositionToleranceMs = 250;
static constexpr int prerollStopDelayMs = 100;
static constexpr int prerollTimeoutMs = 3000;

// Wipes its payload on destruction. This covers only the copy we own: the QNetworkReply
// that produced the bytes keeps its own buffer, and the platform media backend may take a
// further copy of whatever it reads. Treat it as reducing the plaintext residue, not as
// removing it.
class WipingMediaBuffer final : public QBuffer {
public:
    explicit WipingMediaBuffer(QObject* parent)
        : QBuffer(parent)
    {
    }

    ~WipingMediaBuffer() override
    {
        close();
        QByteArray& bytes = buffer();
        volatile char* data = bytes.data();
        for (int i = 0; i < bytes.size(); ++i)
            data[i] = 0;
    }
};

Ref<MediaPlayerPrivateInterface> MediaPlayerPrivateQt::create(MediaPlayer* player)
{
    return adoptRef(*new MediaPlayerPrivateQt(player));
}

class MediaPlayerFactoryQt final : public MediaPlayerFactory {
private:
    MediaPlayerEnums::MediaEngineIdentifier identifier() const final { return MediaPlayerEnums::MediaEngineIdentifier::Qt; };

    Ref<MediaPlayerPrivateInterface> createMediaEnginePlayer(MediaPlayer* player) const final
    {
        return MediaPlayerPrivateQt::create(player);
    }

    void getSupportedTypes(HashSet<String>& types) const final
    {
        return MediaPlayerPrivateQt::getSupportedTypes(types);
    }

    MediaPlayer::SupportsType supportsTypeAndCodecs(const MediaEngineSupportParameters& parameters) const final
    {
        return MediaPlayerPrivateQt::supportsType(parameters);
    }
};

void MediaPlayerPrivateQt::registerMediaEngine(MediaEngineRegistrar registrar)
{
    registrar(makeUnique<MediaPlayerFactoryQt>());
}

void MediaPlayerPrivateQt::getSupportedTypes(HashSet<String>& supported)
{
    QStringList types = QMediaPlayer::supportedMimeTypes();

    for (int i = 0; i < types.size(); i++) {
        QString mime = types.at(i);
        if (mime.startsWith(QString::fromLatin1("audio/")) || mime.startsWith(QString::fromLatin1("video/")))
            supported.add(mime);
    }
}

MediaPlayer::SupportsType MediaPlayerPrivateQt::supportsType(const MediaEngineSupportParameters& parameters)
{
    if (parameters.isMediaStream || parameters.isMediaSource)
        return MediaPlayer::SupportsType::IsNotSupported;

    if (!parameters.type.raw().startsWithIgnoringASCIICase("audio/"_s) && !parameters.type.raw().startsWithIgnoringASCIICase("video/"_s))
        return MediaPlayer::SupportsType::IsNotSupported;

    // Parse and trim codecs
    QStringList codecList;
    for (const String& codec : parameters.type.codecs())
        codecList.append(codec);

    if (QMediaPlayer::hasSupport(parameters.type.containerType(), codecList) >= QMultimedia::ProbablySupported)
        return MediaPlayer::SupportsType::IsSupported;

    return MediaPlayer::SupportsType::MayBeSupported;
}

MediaPlayerPrivateQt::MediaPlayerPrivateQt(MediaPlayer* player)
    : m_webCorePlayer(player)
    , m_mediaPlayer(new QMediaPlayer)
    , m_mediaPlayerControl(0)
    , m_networkState(MediaPlayer::NetworkState::Empty)
    , m_readyState(MediaPlayer::ReadyState::HaveNothing)
    , m_currentSize(0, 0)
    , m_naturalSize(RenderVideo::defaultSize())
    , m_isVisible(false)
    , m_isSeeking(false)
    , m_composited(false)
    , m_preload(MediaPlayer::Preload::Auto)
    , m_bytesLoadedAtLastDidLoadingProgress(0)
    , m_delayingLoad(false)
    , m_suppressNextPlaybackChanged(false)
{
    m_mediaPlayer->setVideoOutput(this);

    // Signal Handlers
    connect(m_mediaPlayer, SIGNAL(mediaStatusChanged(QMediaPlayer::MediaStatus)),
            this, SLOT(mediaStatusChanged(QMediaPlayer::MediaStatus)));
    connect(m_mediaPlayer, SIGNAL(stateChanged(QMediaPlayer::State)),
            this, SLOT(stateChanged(QMediaPlayer::State)));
    connect(m_mediaPlayer, SIGNAL(error(QMediaPlayer::Error)),
            this, SLOT(handleError(QMediaPlayer::Error)));
    connect(m_mediaPlayer, SIGNAL(bufferStatusChanged(int)),
            this, SLOT(bufferStatusChanged(int)));
    connect(m_mediaPlayer, SIGNAL(durationChanged(qint64)),
            this, SLOT(durationChanged(qint64)));
    connect(m_mediaPlayer, SIGNAL(positionChanged(qint64)),
            this, SLOT(positionChanged(qint64)));
    connect(m_mediaPlayer, SIGNAL(volumeChanged(int)),
            this, SLOT(volumeChanged(int)));
    connect(m_mediaPlayer, SIGNAL(mutedChanged(bool)),
            this, SLOT(mutedChanged(bool)));
    connect(this, SIGNAL(surfaceFormatChanged(const QVideoSurfaceFormat&)),
            this, SLOT(surfaceFormatChanged(const QVideoSurfaceFormat&)));

    // Grab the player control
    if (QMediaService* service = m_mediaPlayer->service()) {
        m_mediaPlayerControl = qobject_cast<QMediaPlayerControl *>(
                service->requestControl(QMediaPlayerControl_iid));
    }
}

MediaPlayerPrivateQt::~MediaPlayerPrivateQt()
{
    m_mediaPlayer->disconnect(this);
    m_mediaPlayer->stop();
    clearMedia();

    delete m_mediaPlayer;
}

bool MediaPlayerPrivateQt::hasVideo() const
{
    return m_mediaPlayer->isVideoAvailable();
}

bool MediaPlayerPrivateQt::hasAudio() const
{
    return true;
}

void MediaPlayerPrivateQt::load(const String& url)
{
    m_mediaUrl = url;
    m_delayingLoad = false;

    // QtMultimedia does not have an API to throttle loading
    // so we handle this ourselves by delaying the load
    if (m_preload == MediaPlayer::Preload::None) {
        m_delayingLoad = true;
        return;
    }

    commitLoad(url);
}

void MediaPlayerPrivateQt::commitLoad(const String& url)
{
    clearMedia();
    m_delayingLoad = false;

    // We are now loading
    if (m_networkState != MediaPlayer::NetworkState::Loading) {
        m_networkState = MediaPlayer::NetworkState::Loading;
        m_webCorePlayer->networkStateChanged();
    }

    // And we don't have any data yet
    if (m_readyState != MediaPlayer::ReadyState::HaveNothing) {
        m_readyState = MediaPlayer::ReadyState::HaveNothing;
        m_webCorePlayer->readyStateChanged();
    }

    URL kUrl({ }, url);
    const QUrl rUrl = kUrl;
    const QString scheme = rUrl.scheme().toLower();
    QNetworkRequest request(rUrl);

    Document* document = m_webCorePlayer->owningDocument();
    LocalFrame* frame = document ? document->frame() : nullptr;
    FrameLoader* frameLoader = frame ? &frame->loader() : nullptr;
    NetworkingContext* networkingContext = frameLoader ? frameLoader->networkingContext() : nullptr;
    QNetworkAccessManager* manager = networkingContext ? networkingContext->networkAccessManager() : nullptr;

    // Construct the media content with a network request if the resource is http[s]
    if (scheme == QString::fromLatin1("http") || scheme == QString::fromLatin1("https")) {
        if (manager) {
            // Set the cookies
            QNetworkCookieJar* jar = manager->cookieJar();
            QList<QNetworkCookie> cookies = jar->cookiesForUrl(rUrl);

            // Don't set the header if there are no cookies.
            // This prevents a warning from being emitted.
            if (!cookies.isEmpty())
                request.setHeader(QNetworkRequest::CookieHeader, QVariant::fromValue(cookies));

            // Set the refferer, but not when requesting insecure content from a secure page
            QUrl documentUrl = QUrl(QString(document->documentURI()));
            if (documentUrl.scheme().toLower() == QString::fromLatin1("http") || scheme == QString::fromLatin1("https"))
                request.setRawHeader("Referer", documentUrl.toEncoded());

            // Set the user agent
            request.setRawHeader("User-Agent", frameLoader->userAgent(rUrl).utf8().data());
        }

        m_mediaPlayer->setMedia(QMediaContent(request));
    } else if (scheme != QString::fromLatin1("file") && manager) {
        QNetworkReply* reply = manager->get(request);
        m_pendingMediaReply = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            customMediaReplyFinished(reply);
        });
        return;
    } else {
        // Otherwise, just use the URL
        m_mediaPlayer->setMedia(QMediaContent(rUrl));
    }

    startPlayback();
}

void MediaPlayerPrivateQt::clearMedia()
{
    m_isSeeking = false;
    m_playbackRequested = false;
    m_resumePlaybackAfterSeek = false;
    ++m_seekGeneration;

    if (QNetworkReply* reply = m_pendingMediaReply.data()) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    m_pendingMediaReply.clear();

    m_mediaPlayer->setMedia(QMediaContent());
    endPreroll();

    // setMedia() only *starts* the backend's teardown; on Windows the WMF worker can still
    // be reading the device when it returns, and the destructor deletes m_mediaPlayer right
    // after calling us. deleteLater() keeps the buffer alive past both.
    if (QBuffer* buffer = m_mediaBuffer.data())
        buffer->deleteLater();
    m_mediaBuffer.clear();
}

void MediaPlayerPrivateQt::customMediaReplyFinished(QNetworkReply* reply)
{
    if (m_pendingMediaReply != reply) {
        reply->deleteLater();
        return;
    }

    m_pendingMediaReply.clear();
    const QNetworkReply::NetworkError error = reply->error();
    const QUrl url = reply->request().url();
    if (error != QNetworkReply::NoError) {
        reply->deleteLater();
        reportNetworkError();
        return;
    }

    // The whole asset is held in memory for the lifetime of the element. That is what buys
    // seeking: a QNetworkReply is sequential, so handing it to the player directly would
    // leave the scrubber dead. The cost is a contiguous allocation the size of the media in
    // a 32-bit address space. Lifting it needs a random-access QIODevice that decrypts
    // ranges from the container on demand.
    QByteArray payload = reply->readAll();
    reply->deleteLater();
    // Parented, so the wipe is guaranteed to run: deleteLater() alone would leave the buffer
    // alive if nothing pumps the event loop again, and the reader tears its window down after
    // exec() returns. As a child it also outlives m_mediaPlayer, which the destructor deletes
    // in its body while children go afterwards.
    auto* mediaBuffer = new WipingMediaBuffer(this);
    mediaBuffer->buffer().swap(payload);
    if (!mediaBuffer->open(QIODevice::ReadOnly)) {
        delete mediaBuffer;
        reportNetworkError();
        return;
    }

    m_mediaBuffer = mediaBuffer;
    m_mediaPlayer->setMedia(QMediaContent(url), mediaBuffer);
    startPlayback();
}

void MediaPlayerPrivateQt::reportNetworkError()
{
    const MediaPlayer::NetworkState oldNetworkState = m_networkState;
    const MediaPlayer::ReadyState oldReadyState = m_readyState;
    m_networkState = MediaPlayer::NetworkState::NetworkError;
    m_readyState = MediaPlayer::ReadyState::HaveNothing;
    if (m_readyState != oldReadyState)
        m_webCorePlayer->readyStateChanged();
    if (m_networkState != oldNetworkState)
        m_webCorePlayer->networkStateChanged();
}

void MediaPlayerPrivateQt::startPlayback()
{
    // Set the current volume and mute status
    // We get these from the element, rather than the player, in case we have
    // transitioned from a media engine which doesn't support muting, to a media
    // engine which does.
    m_mediaPlayer->setMuted(m_webCorePlayer->muted());
    m_mediaPlayer->setVolume(static_cast<int>(m_webCorePlayer->volume() * 100.0));

    // Setting a media source starts loading it, but this Qt backend also needs playback-based
    // pre-roll to finish loading audio metadata and to obtain a video's first frame and size.
    // Keep that internal playback muted and invisible to the element.
    m_prerollState = m_webCorePlayer->paused() ? PrerollState::Active : PrerollState::Inactive;
    if (isPrerolling()) {
        m_mediaPlayer->setMuted(true);

        const unsigned generation = ++m_prerollGeneration;
        QTimer::singleShot(prerollTimeoutMs, this, [this, generation]() {
            if (m_prerollState == PrerollState::Active
                && m_prerollGeneration == generation)
                stopPreroll();
        });
    }

    // Don't send PlaybackChanged notification for internal pre-roll.
    m_suppressNextPlaybackChanged = true;
    m_mediaPlayer->play();
}

void MediaPlayerPrivateQt::stopPreroll()
{
    if (m_prerollState != PrerollState::Active)
        return;

    m_prerollState = PrerollState::WaitingToPause;
    const unsigned generation = ++m_prerollGeneration;
    QTimer::singleShot(0, this, [this, generation]() {
        if (m_prerollState != PrerollState::WaitingToPause
            || m_prerollGeneration != generation)
            return;

        // mediaStatusChanged() is delivered from inside WMF's start-completion callback. Let
        // that callback finish before asking it to pause, otherwise pause is queued behind start.
        m_prerollState = PrerollState::WaitingToUnmute;
        if (m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
            m_suppressNextPlaybackChanged = true;
            m_mediaPlayer->pause();
        }

        // QMediaPlayer changes its logical state before WMF finishes pausing. Keep the backend
        // muted across that gap, otherwise the tail of the pre-roll can escape as a short beep.
        QTimer::singleShot(prerollStopDelayMs, this, [this, generation]() {
            if (m_prerollState != PrerollState::WaitingToUnmute
                || m_prerollGeneration != generation)
                return;

            endPreroll();
            if (m_playbackRequested && m_mediaPlayer->state() != QMediaPlayer::PlayingState)
                m_mediaPlayer->play();
        });
    });
}

void MediaPlayerPrivateQt::resumeLoad()
{
    m_delayingLoad = false;

    if (!m_mediaUrl.isNull())
        commitLoad(m_mediaUrl);
}

void MediaPlayerPrivateQt::cancelLoad()
{
    clearMedia();
    m_delayingLoad = false;
    updateStates();
}

void MediaPlayerPrivateQt::prepareToPlay()
{
    if (m_delayingLoad || (m_mediaPlayer->media().isNull() && m_pendingMediaReply.isNull()))
        resumeLoad();
}

void MediaPlayerPrivateQt::play()
{
    m_playbackRequested = true;
    if (m_isSeeking)
        m_resumePlaybackAfterSeek = true;
    if (m_prerollState == PrerollState::WaitingToUnmute)
        return;
    endPreroll();
    if (m_mediaPlayer->state() != QMediaPlayer::PlayingState)
        m_mediaPlayer->play();
}

void MediaPlayerPrivateQt::pause()
{
    m_playbackRequested = false;
    m_resumePlaybackAfterSeek = false;
    if (isPrerolling()) {
        stopPreroll();
        return;
    }
    if (m_mediaPlayer->state() == QMediaPlayer::PlayingState)
        m_mediaPlayer->pause();
}

bool MediaPlayerPrivateQt::paused() const
{
    return (isPrerolling() || m_mediaPlayer->state() != QMediaPlayer::PlayingState);
}

void MediaPlayerPrivateQt::seekToTarget(const SeekTarget& target)
{
    const float position = target.time.toFloat();

    // The element is already in its seeking state by the time we get here and will stay there
    // until timeChanged() reports back. Returning quietly leaves it waiting forever, which
    // stalls playback after a few scrubs - so every path below has to notify.
    // QMediaPlayer reports the media as not seekable while it sits stopped at the end
    // of a clip, which is exactly when a viewer clicks the progress bar to watch it
    // again. Attempt the seek regardless and let the backend answer; only give up when
    // there is no media at all, and report back so the element does not wait forever.
    if (m_mediaPlayer->mediaStatus() == QMediaPlayer::NoMedia
        || m_mediaPlayer->mediaStatus() == QMediaPlayer::UnknownMediaStatus) {
        m_isSeeking = false;
        m_webCorePlayer->timeChanged();
        return;
    }

    // Do not refuse positions that are not buffered yet: seeking ahead of the buffer is normal
    // scrubbing, and the backend fetches what it needs.
    if (!m_isSeeking)
        m_resumePlaybackAfterSeek = m_playbackRequested;
    m_isSeeking = true;
    const unsigned generation = ++m_seekGeneration;
    m_seekTargetPosition = static_cast<qint64>(position * 1000);
    m_mediaPlayer->setPosition(m_seekTargetPosition);

    // positionChanged() is the only signal that ends a seek, and the backend does not always
    // send one: a seek that lands where the clip already sits, or rapid back-and-forth scrubs
    // that coalesce, produce none. Without a fallback m_isSeeking stays set, the element waits
    // forever for timeChanged(), and playback state stops being reported at all - playback
    // appears to stick after a few scrubs. The generation check keeps a stale watchdog from
    // ending a newer seek.
    QTimer::singleShot(1500, this, [this, generation]() {
        if (m_isSeeking && m_seekGeneration == generation)
            finishSeek();
    });
}

void MediaPlayerPrivateQt::finishSeek()
{
    const bool resumePlayback = m_resumePlaybackAfterSeek;
    m_resumePlaybackAfterSeek = false;

    // WMF can stay in PlayingState while its presentation clock is stalled after a backward
    // seek. Restart it even when the state says it is already playing. Keep m_isSeeking set
    // through both calls so their state changes stay invisible to the element.
    if (resumePlayback) {
        if (m_mediaPlayer->state() == QMediaPlayer::PlayingState)
            m_mediaPlayer->pause();
        m_mediaPlayer->play();
    }

    m_isSeeking = false;

    // timeChanged() completes WebCore's seek and schedules its normal play-state reconciliation.
    // Reporting playbackStateChanged() here would expose the transient backend state and make
    // HTMLMediaElement::mediaPlayerPlaybackStateChanged() pause the element permanently.
    m_webCorePlayer->timeChanged();
}

bool MediaPlayerPrivateQt::seeking() const
{
    return m_isSeeking;
}

float MediaPlayerPrivateQt::duration() const
{
    if (m_readyState < MediaPlayer::ReadyState::HaveMetadata)
        return 0.0f;

    float duration = m_mediaPlayer->duration() / 1000.0f;

    // We are streaming
    if (duration <= 0.0f)
        duration = std::numeric_limits<float>::infinity();

    return duration;
}

float MediaPlayerPrivateQt::currentTime() const
{
    return m_mediaPlayer->position() / 1000.0f;
}

const PlatformTimeRanges& MediaPlayerPrivateQt::buffered() const
{
    // The interface hands out a reference now, so the ranges live in the backend.
    m_buffered.clear();
    auto* buffered = &m_buffered;

    if (!m_mediaPlayerControl) {
        // Same missing control as in maxTimeSeekable(): report what is actually loaded
        // rather than nothing, so the element does not think the media is unbuffered.
        const qint64 duration = m_mediaPlayer->duration();
        const auto status = m_mediaPlayer->mediaStatus();
        // LoadedMedia is not "fully buffered" in Qt, so claiming the whole duration on it would
        // overstate progressively downloaded media. It is only safe when we are playing from
        // m_mediaBuffer, where the entire asset was fetched before playback started.
        const bool wholeAssetInMemory = !m_mediaBuffer.isNull() && status == QMediaPlayer::LoadedMedia;
        if (duration > 0
            && (status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::EndOfMedia
                || wholeAssetInMemory)) {
            m_buffered.add(MediaTime::zeroTime(), MediaTime::createWithDouble(duration / 1000.0));
        }
        return m_buffered;
    }

    QMediaTimeRange playbackRanges = m_mediaPlayerControl->availablePlaybackRanges();

    foreach (const QMediaTimeInterval interval, playbackRanges.intervals()) {
        float rangeMin = static_cast<float>(interval.start()) / 1000.0f;
        float rangeMax = static_cast<float>(interval.end()) / 1000.0f;
        buffered->add(MediaTime::createWithFloat(rangeMin),
                      MediaTime::createWithFloat(rangeMax));
    }

    return m_buffered;
}

float MediaPlayerPrivateQt::maxTimeSeekable() const
{
    if (m_mediaPlayerControl) {
        const float latest = static_cast<float>(m_mediaPlayerControl->availablePlaybackRanges().latestTime()) / 1000.0f;
        if (latest > 0)
            return latest;
    }

    // The control object is only available from backends that publish one through
    // QMediaService; without it this reported 0, which tells the element the media is
    // seekable at 0 and nowhere else - so replaying worked while every skip or scrub to
    // another position was refused. A seekable player can reach anywhere in the media.
    if (m_mediaPlayer->isSeekable() || m_mediaPlayer->mediaStatus() == QMediaPlayer::EndOfMedia) {
        const qint64 duration = m_mediaPlayer->duration();
        if (duration > 0)
            return static_cast<float>(duration) / 1000.0f;
    }

    return 0;
}

bool MediaPlayerPrivateQt::didLoadingProgress() const
{
    unsigned bytesLoaded = 0;
    QLatin1String bytesLoadedKey("bytes-loaded");
    if (m_mediaPlayer->availableMetaData().contains(bytesLoadedKey))
        bytesLoaded = m_mediaPlayer->metaData(bytesLoadedKey).toInt();
    else
        bytesLoaded = m_mediaPlayer->bufferStatus();
    bool didLoadingProgress = bytesLoaded != m_bytesLoadedAtLastDidLoadingProgress;
    m_bytesLoadedAtLastDidLoadingProgress = bytesLoaded;
    return didLoadingProgress;
}

unsigned long long MediaPlayerPrivateQt::totalBytes() const
{
    if (m_mediaPlayer->availableMetaData().contains(QMediaMetaData::Size))
        return m_mediaPlayer->metaData(QMediaMetaData::Size).toInt();

    return 100;
}

void MediaPlayerPrivateQt::setPreload(MediaPlayer::Preload preload)
{
    m_preload = preload;
    if (m_delayingLoad && m_preload != MediaPlayer::Preload::None)
        resumeLoad();
}

void MediaPlayerPrivateQt::setRate(float rate)
{
    m_mediaPlayer->setPlaybackRate(rate);
}

void MediaPlayerPrivateQt::setVolume(float volume)
{
    m_mediaPlayer->setVolume(static_cast<int>(volume * 100.0));
}

void MediaPlayerPrivateQt::setMuted(bool muted)
{
    // Applying this mid-pre-roll would undo the internal mute and make the burst audible again.
    // endPreroll() applies whatever the element wants once the pre-roll is over.
    if (isPrerolling())
        return;
    m_mediaPlayer->setMuted(muted);
}

// Ends the pre-roll and hands audio back to the element. Every path out of a pre-roll goes
// through here so a silenced player cannot stay silent.
void MediaPlayerPrivateQt::endPreroll()
{
    if (!isPrerolling())
        return;

    m_prerollState = PrerollState::Inactive;
    ++m_prerollGeneration;
    m_mediaPlayer->setMuted(m_webCorePlayer->muted());
    m_mediaPlayer->setVolume(static_cast<int>(m_webCorePlayer->volume() * 100.0));
}

MediaPlayer::NetworkState MediaPlayerPrivateQt::networkState() const
{
    return m_networkState;
}

MediaPlayer::ReadyState MediaPlayerPrivateQt::readyState() const
{
    return m_readyState;
}

void MediaPlayerPrivateQt::setPageIsVisible(bool, String&&)
{
}

void MediaPlayerPrivateQt::mediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    // Pre-roll done
    if (m_prerollState == PrerollState::Active
        && (status == QMediaPlayer::BufferingMedia || status == QMediaPlayer::BufferedMedia))
        stopPreroll();

    updateStates();

    // The element decides that playback ended by inspecting the current time when the
    // backend reports one. Without this the ended event never fires: the controls stay
    // showing pause, the position is never reset, and the element still believes it is
    // playing, so interacting with it afterwards does nothing.
    if (status == QMediaPlayer::InvalidMedia || status == QMediaPlayer::NoMedia)
        endPreroll();

    if (status == QMediaPlayer::EndOfMedia) {
        endPreroll();
        m_isSeeking = false;
        m_resumePlaybackAfterSeek = false;
        m_webCorePlayer->timeChanged();
    }
}

void MediaPlayerPrivateQt::handleError(QMediaPlayer::Error)
{
    // A failed pre-roll must still hand audio back, or the element stays silently muted.
    endPreroll();
    updateStates();
}

void MediaPlayerPrivateQt::stateChanged(QMediaPlayer::State state)
{
    // A seek makes the backend leave PlayingState while it repositions, and paused() is derived
    // from that state - so reporting this transition tells the element the user paused, which is
    // sticky: playback stops mid-clip and needs a click to resume. The transition is our own
    // artifact, so swallow it and let the seek completion decide (see positionChanged).
    if (m_isSeeking)
        return;

    // The platform player may deliver a delayed state from the pause/seek/play sequence after
    // the seek itself has completed. WebCore's latest request is authoritative; feeding that
    // stale pause back would permanently pause the element. Re-apply the request instead.
    const QMediaPlayer::MediaStatus status = m_mediaPlayer->mediaStatus();
    const bool terminalStatus = status == QMediaPlayer::EndOfMedia
        || status == QMediaPlayer::InvalidMedia
        || status == QMediaPlayer::NoMedia;
    if (!isPrerolling() && !terminalStatus) {
        if (m_playbackRequested && state != QMediaPlayer::PlayingState) {
            m_mediaPlayer->play();
            return;
        }
        if (!m_playbackRequested && state == QMediaPlayer::PlayingState) {
            m_mediaPlayer->pause();
            return;
        }
    }

    if (!m_suppressNextPlaybackChanged)
        m_webCorePlayer->playbackStateChanged();
    else
        m_suppressNextPlaybackChanged = false;
}

void MediaPlayerPrivateQt::surfaceFormatChanged(const QVideoSurfaceFormat& format)
{
    QSize size = format.sizeHint();
    LOG(Media, "MediaPlayerPrivateQt::naturalSizeChanged(%dx%d)",
            size.width(), size.height());

    if (!size.isValid())
        return;

    IntSize webCoreSize = size;
    if (webCoreSize == m_naturalSize)
        return;

    m_naturalSize = webCoreSize;
    m_webCorePlayer->sizeChanged();
}

void MediaPlayerPrivateQt::positionChanged(qint64 position)
{
    // A normal playback tick queued before the latest setPosition() must not complete that seek.
    // Backends may snap slightly around the requested timestamp, hence the small tolerance.
    if (m_isSeeking && qAbs(position - m_seekTargetPosition) <= seekPositionToleranceMs)
        finishSeek();
}

void MediaPlayerPrivateQt::bufferStatusChanged(int)
{
    notImplemented();
}

void MediaPlayerPrivateQt::durationChanged(qint64)
{
    m_webCorePlayer->durationChanged();
}

void MediaPlayerPrivateQt::volumeChanged(int volume)
{
    m_webCorePlayer->volumeChanged(static_cast<float>(volume) / 100.0);
}

void MediaPlayerPrivateQt::mutedChanged(bool muted)
{
    // The pre-roll mute is ours, not the page's: forwarding it would mute the element and show
    // it muted in the controls.
    if (isPrerolling())
        return;
    m_webCorePlayer->muteChanged(muted);
}

void MediaPlayerPrivateQt::updateStates()
{
    // Store the old states so that we can detect a change and raise change events
    MediaPlayer::NetworkState oldNetworkState = m_networkState;
    MediaPlayer::ReadyState oldReadyState = m_readyState;

    QMediaPlayer::MediaStatus currentStatus = m_mediaPlayer->mediaStatus();
    QMediaPlayer::Error currentError = m_mediaPlayer->error();

    if (currentError != QMediaPlayer::NoError) {
        m_readyState = MediaPlayer::ReadyState::HaveNothing;
        if (currentError == QMediaPlayer::FormatError || currentError == QMediaPlayer::ResourceError)
            m_networkState = MediaPlayer::NetworkState::FormatError;
        else
            m_networkState = MediaPlayer::NetworkState::NetworkError;
    } else if (currentStatus == QMediaPlayer::UnknownMediaStatus
               || currentStatus == QMediaPlayer::NoMedia) {
        m_networkState = MediaPlayer::NetworkState::Idle;
        m_readyState = MediaPlayer::ReadyState::HaveNothing;
    } else if (currentStatus == QMediaPlayer::LoadingMedia) {
        m_networkState = MediaPlayer::NetworkState::Loading;
        m_readyState = MediaPlayer::ReadyState::HaveNothing;
    } else if (currentStatus == QMediaPlayer::LoadedMedia) {
        m_networkState = MediaPlayer::NetworkState::Loading;
        m_readyState = MediaPlayer::ReadyState::HaveMetadata;
    } else if (currentStatus == QMediaPlayer::BufferingMedia) {
        m_networkState = MediaPlayer::NetworkState::Loading;
        m_readyState = MediaPlayer::ReadyState::HaveFutureData;
    } else if (currentStatus == QMediaPlayer::StalledMedia) {
        m_networkState = MediaPlayer::NetworkState::Loading;
        m_readyState = MediaPlayer::ReadyState::HaveCurrentData;
    } else if (currentStatus == QMediaPlayer::BufferedMedia
               || currentStatus == QMediaPlayer::EndOfMedia) {
        m_networkState = MediaPlayer::NetworkState::Loaded;
        m_readyState = MediaPlayer::ReadyState::HaveEnoughData;
    } else if (currentStatus == QMediaPlayer::InvalidMedia) {
        m_networkState = MediaPlayer::NetworkState::FormatError;
        m_readyState = MediaPlayer::ReadyState::HaveNothing;
    }

    // Log the state changes and raise the state change events
    // NB: The readyStateChanged event must come before the networkStateChanged event.
    // Breaking this invariant will cause the resource selection algorithm for multiple
    // sources to fail.
    if (m_readyState != oldReadyState)
        m_webCorePlayer->readyStateChanged();

    if (m_networkState != oldNetworkState)
        m_webCorePlayer->networkStateChanged();
}

void MediaPlayerPrivateQt::setPresentationSize(const IntSize& size)
{
    LOG(Media, "MediaPlayerPrivateQt::setSize(%dx%d)",
            size.width(), size.height());

    if (size == m_currentSize)
        return;

    m_currentSize = size;
}

FloatSize MediaPlayerPrivateQt::naturalSize() const
{
    if (!hasVideo() ||  m_readyState < MediaPlayer::ReadyState::HaveMetadata) {
        LOG(Media, "MediaPlayerPrivateQt::naturalSize() -> 0x0 (!hasVideo || !haveMetaData)");
        return IntSize();
    }

    LOG(Media, "MediaPlayerPrivateQt::naturalSize() -> %dx%d (m_naturalSize)",
            m_naturalSize.width(), m_naturalSize.height());

    return m_naturalSize;
}

void MediaPlayerPrivateQt::removeVideoItem()
{
    m_mediaPlayer->setVideoOutput(static_cast<QAbstractVideoSurface*>(0));
}

void MediaPlayerPrivateQt::restoreVideoItem()
{
    m_mediaPlayer->setVideoOutput(this);
}

// Begin QAbstractVideoSurface implementation.

bool MediaPlayerPrivateQt::start(const QVideoSurfaceFormat& format)
{
    m_currentVideoFrame = QVideoFrame();
    m_frameFormat = format;

    // If the pixel format is not supported by QImage, then we return false here and the QtMultimedia back-end
    // will re-negotiate and call us again with a better format.
    if (QVideoFrame::imageFormatFromPixelFormat(m_frameFormat.pixelFormat()) == QImage::Format_Invalid)
        return false;

    return QAbstractVideoSurface::start(format);
}

QList<QVideoFrame::PixelFormat> MediaPlayerPrivateQt::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const
{
    QList<QVideoFrame::PixelFormat> formats;
    switch (handleType) {
    case QAbstractVideoBuffer::QPixmapHandle:
    case QAbstractVideoBuffer::NoHandle:
        formats << QVideoFrame::Format_RGB32 << QVideoFrame::Format_ARGB32 << QVideoFrame::Format_RGB565;
        break;
    default: break;
    }
    return formats;
}

bool MediaPlayerPrivateQt::present(const QVideoFrame& frame)
{
    m_currentVideoFrame = frame;
    m_webCorePlayer->repaint();
    return true;
}

// End QAbstractVideoSurface implementation.

void MediaPlayerPrivateQt::paint(GraphicsContext& context, const FloatRect& rect)
{
    if (m_composited)
        return;
    paintCurrentFrameInContext(context, rect);
}

void MediaPlayerPrivateQt::paintCurrentFrameInContext(GraphicsContext& context, const FloatRect& rect)
{
    if (context.paintingDisabled())
        return;

    if (!m_currentVideoFrame.isValid())
        return;

    QPainter* painter = context.platformContext()->painter();

    if (m_currentVideoFrame.handleType() == QAbstractVideoBuffer::QPixmapHandle) {
        painter->drawPixmap(QRectF(rect), m_currentVideoFrame.handle().value<QPixmap>(), QRectF(0, 0, rect.width(), rect.height()));
    } else if (m_currentVideoFrame.map(QAbstractVideoBuffer::ReadOnly)) {
        QImage image(m_currentVideoFrame.bits(),
                     m_frameFormat.frameSize().width(),
                     m_frameFormat.frameSize().height(),
                     m_currentVideoFrame.bytesPerLine(),
                     QVideoFrame::imageFormatFromPixelFormat(m_frameFormat.pixelFormat()));
        const QRectF target = rect;

        if (m_frameFormat.scanLineDirection() == QVideoSurfaceFormat::BottomToTop) {
            const QTransform oldTransform = painter->transform();
            painter->scale(1, -1);
            painter->translate(0, -target.bottom());
            painter->drawImage(QRectF(target.x(), 0, target.width(), target.height()), image);
            painter->setTransform(oldTransform);
        } else {
            painter->drawImage(target, image);
        }

        m_currentVideoFrame.unmap();
    }
}

void MediaPlayerPrivateQt::paintToTextureMapper(TextureMapper& textureMapper, const FloatRect& targetRect, const TransformationMatrix& matrix, float opacity)
{
}

} // namespace WebCore

#include "moc_MediaPlayerPrivateQt.cpp"
