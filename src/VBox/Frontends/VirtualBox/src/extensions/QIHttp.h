/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * QIHttp class declaration & implementation
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __QIHttp_h__
#define __QIHttp_h__

/* Qt includes */
#include <QHttp>
#include <QTimer>

/* Time to auto-disconnect if no network answer received. */
static const int MaxWaitTime = 20000;

/* This is QHttp extension to unite different happens errors
 * into one already present error processing mechanism. */
class QIHttp : public QHttp
{
    Q_OBJECT;

public:

    /* Additional error codes */
    enum AdvancedError
    {
        /* Basic QHttp errors */
        NoError                          = QHttp::NoError,
        UnknownError                     = QHttp::UnknownError,
        HostNotFound                     = QHttp::HostNotFound,
        ConnectionRefused                = QHttp::ConnectionRefused,
        UnexpectedClose                  = QHttp::UnexpectedClose,
        InvalidResponseHeader            = QHttp::InvalidResponseHeader,
        WrongContentLength               = QHttp::WrongContentLength,
        Aborted                          = QHttp::Aborted,
        AuthenticationRequiredError      = QHttp::AuthenticationRequiredError,
        ProxyAuthenticationRequiredError = QHttp::ProxyAuthenticationRequiredError,

        /* Advanced QHttp errors */
        TimeoutError,          /* MaxWaitTime time passed with no response */
        PageNotFoundError,     /* Corresponds to 404 == not found header */
        MovedPermanentlyError, /* Corresponds to 301 == moved permanently response */
        MovedTemporarilyError  /* Corresponds to 302 == moved temporarily response */
    };

    QIHttp (QObject *aParent)
        : QHttp (aParent)
        , mStatusCode (0)
        , mErrorCode (NoError)
    {
        mTimeoutTimer.setSingleShot (true);
        mTimeoutTimer.setInterval (MaxWaitTime);
        connect (&mTimeoutTimer, SIGNAL (timeout()),
                 this, SLOT (timeouted()));
        connect (this, SIGNAL (dataReadProgress (int, int)),
                 &mTimeoutTimer, SLOT (start()));
        connect (this, SIGNAL (dataSendProgress (int, int)),
                 &mTimeoutTimer, SLOT (start()));
        connect (this, SIGNAL (done (bool)),
                 &mTimeoutTimer, SLOT (stop()));

        connect (this, SIGNAL (responseHeaderReceived (const QHttpResponseHeader &)),
                 this, SLOT (processResponseHeader (const QHttpResponseHeader &)));
        connect (this, SIGNAL (done (bool)), this, SLOT (processDone (bool)));
    }

    AdvancedError errorCode() const { return mErrorCode; }

    QString errorString() const
    {
        switch (mErrorCode)
        {
            case TimeoutError:
                return tr ("Connection timed out");
            case PageNotFoundError:
                return tr ("Could not locate the file on "
                           "the server (response: %1)").arg (mStatusCode);
            case MovedPermanentlyError:
            case MovedTemporarilyError:
                return QString::null; /* should be redirected anyway */
            default:
                return QHttp::errorString();
        }
    }

    int get (const QString &aPath)
    {
        mTimeoutTimer.start();
        return QHttp::get (aPath, 0);
    }

    int post (const QString &aPath)
    {
        mTimeoutTimer.start();
        return QHttp::post (aPath, 0);
    }

    int request (const QHttpRequestHeader &aHeader)
    {
        mTimeoutTimer.start();
        return QHttp::request (aHeader);
    }

    /* This method is doing effectively the same as abort()
     * but in its turn trying to do all after other things been done. */
    void abortAll()
    {
        QTimer::singleShot(0, this, SLOT(abort()));
    }

signals:

    void allIsDone (bool aError);

private slots:

    /* We should hide this method from direct call as its very
     * dangerous to call it while other requests are being processed.
     * Use abortAll() to get the same result. */
    void abort()
    {
        QHttp::abort();
    }

    void timeouted()
    {
        mErrorCode = TimeoutError;
        abortAll();
    }

    void processResponseHeader (const QHttpResponseHeader &aResponse)
    {
        mStatusCode = aResponse.statusCode();
        switch (mStatusCode)
        {
            case 301:
                mErrorCode = MovedPermanentlyError;
                return abortAll();
            case 302:
                mErrorCode = MovedTemporarilyError;
                return abortAll();
            case 404:
                mErrorCode = PageNotFoundError;
                return abortAll();
            default:
                mErrorCode = (AdvancedError) QHttp::error();
        }
    }

    void processDone (bool aError)
    {
        if (mErrorCode == NoError)
            mErrorCode = (AdvancedError) QHttp::error();
        emit allIsDone (aError);
    }

private:

    QTimer mTimeoutTimer;
    int mStatusCode;
    AdvancedError mErrorCode;
};

#endif // __QIHttp_h__

