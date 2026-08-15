/*
 * Copyright (C) 2007 Staikos Computing Services Inc.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MainThread.h"

#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QThread>
#include <atomic>

namespace WTF {

static int s_mainThreadInvokerEventType;

class MainThreadInvoker : public QObject {
    Q_OBJECT
public:
    MainThreadInvoker();
    bool event(QEvent*) override;
};

MainThreadInvoker::MainThreadInvoker()
{
    s_mainThreadInvokerEventType = QEvent::registerEventType();
}

bool MainThreadInvoker::event(QEvent* e)
{
    if (e->type() != s_mainThreadInvokerEventType)
        return QObject::event(e);

    return true;
}

Q_GLOBAL_STATIC(MainThreadInvoker, webkit_main_thread_invoker)

// QTFIXME: record the main thread explicitly instead of letting whichever thread
// happens to call isMainThread() first define it. Q_GLOBAL_STATIC is lazy, so the
// MainThreadInvoker QObject took the thread affinity of its first caller -- and
// WTF's thread_local destructors call isMainThread() from arbitrary threads as
// they exit. Observed here from a background HTTP worker via __dyn_tls_dtor,
// before initializeWebCoreQt() had run on the main thread: that permanently bound
// "main thread" to the worker, so RELEASE_ASSERT(isMainThread()) in
// MemoryCache::singleton() aborted the process during startup. It also meant
// scheduleDispatchFunctionsOnMainThread() would post to the wrong thread.
static std::atomic<QThread*> s_mainThread { nullptr };

void initializeMainThreadPlatform()
{
    s_mainThread.store(QThread::currentThread(), std::memory_order_release);
    webkit_main_thread_invoker();
}

bool isMainThread()
{
    // Deliberately does not instantiate the invoker; see above.
    if (QThread* mainThread = s_mainThread.load(std::memory_order_acquire))
        return mainThread == QThread::currentThread();

    // Not designated yet. Defer to Qt's own notion of the main thread when there
    // is a QCoreApplication; otherwise no thread can claim to be the main one.
    QCoreApplication* app = QCoreApplication::instance();
    return app && app->thread() == QThread::currentThread();
}

bool isMainThreadIfInitialized()
{
    return isMainThread();
}

bool isMainThreadInitialized()
{
    return true;
}

void scheduleDispatchFunctionsOnMainThread()
{
    QCoreApplication::postEvent(webkit_main_thread_invoker(), new QEvent(static_cast<QEvent::Type>(s_mainThreadInvokerEventType)));
}

} // namespace WTF

#include "MainThreadQt.moc"
