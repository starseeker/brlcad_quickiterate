/*
 * MIT License
 *
 * Copyright (c) 2018 Juan Gonzalez Burgos
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Originally from https://github.com/juangburgos/QConsoleListener
 */

#include "common.h"

#include <iostream>
#include <QApplication>
#include <QThread>
#include "qtcad/QgConsoleListener.h"

#define RT_MAXLINE 10240

void noMessageOutput(QtMsgType, const QMessageLogContext&, const QString&) {}

QConsoleListener::QConsoleListener(int fd, struct ged_subprocess *p, bu_process_io_t t, ged_io_func_t c, void *d)
{
    this->process = p;
    this->callback = c;
    this->data = d;
    this->type = t;

    // finishedGetLine is kept for backward compatibility but is no longer
    // used by the primary code path (on_callbackReady emits newLine directly).
    QObject::connect(
	    this, &QConsoleListener::finishedGetLine,
	    this, &QConsoleListener::on_finishedGetLine,
	    Qt::QueuedConnection
	    );

    // callbackReady: background thread -> main thread handoff.
    // The notifier fires in the background thread and emits callbackReady().
    // on_callbackReady() runs on the main thread (QueuedConnection), so all
    // access to the shared ged_result_str happens only on the main thread.
    QObject::connect(
	    this, &QConsoleListener::callbackReady,
	    this, &QConsoleListener::on_callbackReady,
	    Qt::QueuedConnection
	    );

#ifdef Q_OS_WIN
    HANDLE h = (fd < 0) ? GetStdHandle(STD_INPUT_HANDLE) : (HANDLE)_get_osfhandle(fd);
    m_notifier = new QWinEventNotifier(h);
#else
    int lfd = (fd < 0) ? fileno(stdin) : fd;
    m_notifier = new QSocketNotifier(lfd, QSocketNotifier::Read);
#endif
    // Move the notifier to a background thread so waiting for I/O does not
    // block the main event loop.  All shared state (gedp, ged_result_str) is
    // accessed only in on_callbackReady(), which executes on the main thread.
    m_notifier->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_notifier, &QObject::deleteLater);
#ifdef Q_OS_WIN
    QObject::connect(m_notifier, &QWinEventNotifier::activated, m_notifier,
#else
    QObject::connect(m_notifier, &QSocketNotifier::activated, m_notifier,
#endif
	[this]() {
	// The background thread touches nothing on gedp.  It only signals
	// the main thread that data is ready on the file descriptor.
	if (callback)
	    Q_EMIT this->callbackReady();
	});
    m_thread.start();
}

QConsoleListener::~QConsoleListener()
{
    m_notifier->disconnect();
    m_thread.quit();
    m_thread.wait();
}

void QConsoleListener::on_callbackReady()
{
    // Runs on the main thread (queued from callbackReady signal).
    // All access to the shared ged_result_str is confined to this slot,
    // so there is no data race with other main-thread code that reads or
    // truncates the same buffer.
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    if (!callback)
	return;
    size_t s1 = bu_vls_strlen(process->gedp->ged_result_str);
    (*callback)(data, (int)type);
    size_t s2 = bu_vls_strlen(process->gedp->ged_result_str);
    if (s1 != s2) {
	struct bu_vls nstr = BU_VLS_INIT_ZERO;
	bu_vls_substr(&nstr, process->gedp->ged_result_str, s1, s2 - s1);
	Q_EMIT this->newLine(QString::fromStdString(std::string(bu_vls_cstr(&nstr))));
	bu_vls_free(&nstr);
    }
}

void QConsoleListener::on_finishedGetLine(const QString &strNewLine)
{
    Q_EMIT this->newLine(strNewLine);
}

void QConsoleListener::on_finished()
{
    Q_EMIT this->is_finished(this->process, (int)this->type);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

