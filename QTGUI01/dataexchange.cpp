#include "dataexchange.h"

DataExchange::DataExchange(QObject *parent) :
    QObject(parent)
{
    _serialOn = false;
    _serialMode = 0;
    _radioMode = 0;
    _manualMode = 0;
    serialPortName = "";
    connectionMethod = "";
    auxSerialPortName = "";
    auxConnectionMethod = "";
    for (int i =0; i<3; i++)
    {
        current_gps[i].lat = 0;
        current_gps[i].lon = 0;

        addressList[i] = "";
    }
    initServerWorker();
}

DataExchange::~DataExchange()
{
    _serialOn = false;

    worker->abort();
    thread->wait();
    qDebug()<<"Deleting thread and worker in Thread "<<this->QObject::thread()->currentThreadId();
    delete thread;
    delete worker;

    // local server
    serverWorker->abort();
    serverThread->wait();
    delete serverThread;
    delete serverWorker;
}

void DataExchange::initServerWorker()
{
    // local server
    serverThread = new QThread();
    serverWorker = new LocalServerWorker();
    serverWorker->moveToThread(serverThread);
    connect(serverWorker, SIGNAL(workRequested()), serverThread, SLOT(start()));
    connect(serverThread, SIGNAL(started()), serverWorker, SLOT(doWork()));
    connect(serverWorker, SIGNAL(finished()), serverThread, SLOT(quit()), Qt::DirectConnection);
    serverWorker->requestWork();
}

bool DataExchange::get_serialOn() const
{
    return _serialOn;
}

void DataExchange::set_serialOn(bool value)
{
    if (value != _serialOn)
    {
        _serialOn = value;
        if (_serialOn == true)
        {
            thread = new QThread();
            worker = new SerialWorker (serialPortName, connectionMethod, addressList);
            //worker->setPortName(serialPortName);

            worker->moveToThread(thread);
            connect(worker, SIGNAL(workRequested()), thread, SLOT(start()));
            connect(thread, SIGNAL(started()), worker, SLOT(doWork()));
            connect(worker, SIGNAL(finished()), thread, SLOT(quit()), Qt::DirectConnection);

            connect(worker,SIGNAL(quadsStatesChanged(QList<QuadStates *> *)), this, SLOT(updateQuadsStates(QList<QuadStates *> *)) );

            worker->requestWork();
        }
        else if (_serialOn == false)
        {
            worker->abort();
            thread->wait();
        }
        emit serialOnChanged(_serialOn);
    }
}

int DataExchange::get_serialMode() const
{
    return _serialMode;
}

void DataExchange::set_serialMode(int value)
{
    if (value != _serialMode)
    {
        _serialMode = value;
        worker->modeSelection = _serialMode;
        if (_serialMode == 11)
        {
            worker->mi_list_air[0] = mi_list_air[0];
        }
        else if (_serialMode == 12)
        {
            worker->mi_list_air[1] = mi_list_air[1];
        }
        else if (_serialMode == 13)
        {
            worker->mi_list_air[2] = mi_list_air[2];
        }
    }
}

int DataExchange::get_radioMode() const
{
    return _radioMode;
}

void DataExchange::set_radioMode(int value)
{
    if (value != _radioMode)
    {
        _radioMode = value;
        worker->radioStatus = _radioMode;
    }
}

bool DataExchange::get_auxSerialOn() const
{
    return _auxSerialOn;
}

void DataExchange::set_auxSerialOn(bool value)
{
    if (value != _auxSerialOn)
    {
        _auxSerialOn = value;
        if (_auxSerialOn == true)
        {
            serverWorker->set_serialPortName(auxSerialPortName);
            serverWorker->set_auxSerialOn(true);

            for (int i = 0; i< 3; i++)
            {
                serverWorker->addressList[i] = addressList[i];
            }
        }
        else if (_auxSerialOn == false)
        {
            serverWorker->set_auxSerialOn(false);
            serverWorker->set_serialPortName("");
        }
        emit auxSerialOnChanged(_auxSerialOn);
    }
}

int DataExchange::get_manualMode() const
{
    return _manualMode;
}

void DataExchange::set_manualMode(int value)
{
    if (value != _manualMode)
    {
        _manualMode = value;
        qDebug() << _manualMode;
        serverWorker->set_manualMode(_manualMode);
        //worker->manualStatus = _manualMode;
    }
}

void DataExchange::updateQuadsStates(QList<QuadStates *> *tempObjList)
{
    for (int i = 0; i < tempObjList->length(); i++)
    {
        current_gps[i].lat = tempObjList->at(i)->msp_raw_gps.gpsSol_llh_lat/qPow(10.0, 7);
        current_gps[i].lon = tempObjList->at(i)->msp_raw_gps.gpsSol_llh_lon/qPow(10.0, 7);
    }
    emit quadsStatesChanged(tempObjList);
}

SerialWorker::SerialWorker(QString portName, QString connectionName, QString addrList[], QObject *parent) :
    QObject(parent)
{
    serialPortName = portName;
    connectionMethod = connectionName;
    _working =false;
    _abort = false;
    modeSelection = 0;
    radioStatus = 0;
    manualMode = 0;
    for (int i =0; i<3; i++)
    {
        addressList[i] = addrList[i];
    }
}

void SerialWorker::setPortName(QString portName)
{
    serialPortName = portName;
}

void SerialWorker::requestWork()
{
    mutex.lock();
    _working = true;
    _abort = false;
    qDebug()<<"Request worker start in Thread "<<thread()->currentThreadId();
    mutex.unlock();

    emit workRequested();
}

void SerialWorker::abort()
{
    mutex.lock();
    if (_working) {
        _abort = true;
        qDebug()<<"Request worker aborting in Thread "<<thread()->currentThreadId();
    }
    mutex.unlock();
}

void SerialWorker::doWork()
{
    qDebug()<<"Starting worker process in Thread "<<thread()->currentThreadId();

    mutex.lock();
    bool abort = _abort;
    mutex.unlock();

    serial = new QSerialPort;
    serial->setPortName(serialPortName);

    // connection to serial communication handle
    scHandle = new SerialCommunication(serial, connectionMethod, addressList);
    connect(scHandle,SIGNAL(quadsStatesChanged(QList<QuadStates *> *)), this, SLOT(updateQuadsStates(QList<QuadStates *> *)) );
    // preload required info
    scHandle->PreLoadInfo();

    while (!abort)
    {
        realWorker();

        mutex.lock();
        bool abort = _abort;
        mutex.unlock();
        if (abort) {
            qDebug()<<"Aborting worker process in Thread "<<thread()->currentThreadId();
            break;
        }
    }
    serial->flush();
    if(serial->isOpen())
    {
        serial->close();
    }

    delete serial;
    // Set _working to false, meaning the process can't be aborted anymore.
    mutex.lock();
    _working = false;
    mutex.unlock();

    qDebug()<<"Worker process finished in Thread "<<thread()->currentThreadId();

    emit finished();
}

void SerialWorker::realWorker()
{
    // regular load info
    if (serial->isOpen() && serial->isWritable())
    {
        //qDebug() << "Serial is open";
        if (connectionMethod == "USB")  // super high update frequency and all data fields, use normal serial communication.
        {
            switch (modeSelection) {
            case 0:
            {
                // check
                scHandle->RegularCheck();
                QTime dieTime1= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime1 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                // radio
                scHandle->RegularArmAndNavAll(radioStatus);
                QTime dieTime2= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime2 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                break;
            }
            case 1:  // check quad 1 and all gps, send out radio control signal
            {
                // check
                scHandle->RegularCheck();
                QTime dieTime1= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime1 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                // radio
                scHandle->RegularArmAndNavAll(radioStatus);
                QTime dieTime2= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime2 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                break;
            }
            case 2:  // not used here
            {
                break;
            }
            case 3:  // not used here
            {
                break;
            }
            case 11:
            {
                scHandle->UploadMissions(0, mi_list_air[0]);
                QTime dieTime= QTime::currentTime().addMSecs(500);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                modeSelection = modeSelection - 10;
                break;
            }
            case 12:  // not used here
            {
                break;
            }
            case 13:  // not used here
            {
                break;
            }
            case 21:
            {
                scHandle->DownloadMissions(0);
                QTime dieTime= QTime::currentTime().addMSecs(500);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                modeSelection = modeSelection - 20;
                break;
            }
            case 22:  // not used here
            {
                break;
            }
            case 23:  // not used here
            {
                break;
            }
            default:
            {
                break;
            }
            }
        }
        else if (connectionMethod == "API") // super low update frequency and only very little data fields, need to use API method.
        {
            switch (modeSelection) {
            case 0:
            {
                // check
                scHandle->RegularCheck();
                QTime dieTime1 = QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime1 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                // radio
                scHandle->RegularArmAndNavAll(radioStatus);
                QTime dieTime2= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime2 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                break;
            }
            case 1:  // check quad 1 and all gps, send out radio control signal
            {
                // check
                scHandle->RegularCheck();
                QTime dieTime= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                // radio
                scHandle->RegularArmAndNavAll(radioStatus);
                QTime dieTime2= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime2 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                break;
            }
            case 2:  // not used here
            {
                break;
            }
            case 3:  // not used here
            {
                break;
            }
            case 11:
            {
                scHandle->UploadMissions(0, mi_list_air[0]);
                QTime dieTime= QTime::currentTime().addMSecs(500);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                modeSelection = modeSelection - 10;
                break;
            }
            case 12:  // not used here
            {
                break;
            }
            case 13:  // not used here
            {
                break;
            }
            case 21:
            {
                scHandle->DownloadMissions(0);
                QTime dieTime= QTime::currentTime().addMSecs(500);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                modeSelection = modeSelection - 20;
                break;
            }
            case 22:  // not used here
            {
                break;
            }
            case 23:  // not used here
            {
                break;
            }
            default:
            {
                break;
            }
            }
        }
        else if (connectionMethod == "AT") // medium update frequency and limitted data fields, use normal serial communication.
        {
            switch (modeSelection) {
            case 0:
            {
                // check
                scHandle->RegularCheck();
                QTime dieTime1= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime1 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                // radio
                scHandle->RegularArmAndNavAll(radioStatus);
                QTime dieTime2= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime2 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                break;
            }
            case 1:  // check quad 1 and all gps, send out radio control signal
            {
                // check
                scHandle->RegularCheck();
                QTime dieTime1= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime1 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                // radio
                scHandle->RegularArmAndNavAll(radioStatus);
                QTime dieTime2= QTime::currentTime().addMSecs(100);
                while( QTime::currentTime() < dieTime2 )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                break;
            }
            case 2:  // not used here
            {
                break;
            }
            case 3:  // not used here
            {
                break;
            }
            case 11:
            {
                scHandle->UploadMissions(0, mi_list_air[0]);
                QTime dieTime= QTime::currentTime().addMSecs(500);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                modeSelection = modeSelection - 10;
                break;
            }
            case 12:  // not used here
            {
                break;
            }
            case 13:  // not used here
            {
                break;
            }
            case 21:
            {
                scHandle->DownloadMissions(0);
                QTime dieTime= QTime::currentTime().addMSecs(500);
                while( QTime::currentTime() < dieTime )
                {
                    QEventLoop loop;
                    QTimer::singleShot(1, &loop, SLOT(quit()));
                    loop.exec();
                }
                modeSelection = modeSelection - 20;
                break;
            }
            case 22:  // not used here
            {
                break;
            }
            case 23:  // not used here
            {
                break;
            }
            default:
            {
                break;
            }
            }
        }
    }
}

void SerialWorker::updateQuadsStates(QList<QuadStates *> *tempObjList)
{
    emit quadsStatesChanged(tempObjList);
}


LocalServerWorker::LocalServerWorker(QObject *parent) :
    QObject(parent)
{
    _serialPortName = "cu.usbserial-A602ZF3F";
    connectionMethod = "AT";

    _working = false;
    _abort = false;
    _auxSerialOn = false;
    _manualMode = 0;
    serialReady = false;
    manual_rc_values.rcData[0] = 1500;
    manual_rc_values.rcData[1] = 1500;
    manual_rc_values.rcData[2] = 1000;
    manual_rc_values.rcData[3] = 1500;
    manual_rc_values.rcData[4] = 1000;
    manual_rc_values.rcData[5] = 1000;
    manual_rc_values.rcData[6] = 1000;
    manual_rc_values.rcData[7] = 1000;

    server = new LocalServer;
    connect(server, SIGNAL(inputReceived(QString)), this, SLOT(updateRCValues(QString)) );
}

void LocalServerWorker::requestWork()
{
    mutex.lock();
    _working = true;
    _abort = false;
    qDebug()<<"Request server worker start in Thread "<<thread()->currentThreadId();
    mutex.unlock();

    emit workRequested();
}

void LocalServerWorker::abort()
{
    mutex.lock();
    if (_working) {
        _abort = true;
        qDebug()<<"Request server worker aborting in Thread "<<thread()->currentThreadId();
    }
    mutex.unlock();
}

void LocalServerWorker::doWork()
{
    qDebug()<<"Starting server worker process in Thread "<<thread()->currentThreadId();

    mutex.lock();
    bool abort = _abort;
    mutex.unlock();

    /*
    server = new LocalServer;
    connect(server, SIGNAL(inputReceived(QString)), this, SLOT(updateRCValues(QString)) );
    */

    // serial

    while (!abort)
    {
        realWorker();

        mutex.lock();
        bool abort = _abort;
        mutex.unlock();
        if (abort)
        {
            qDebug()<<"Aborting server worker process in Thread "<<thread()->currentThreadId();
            break;
        }
    }

    serial->flush();
    if(serial->isOpen())
    {
        serial->close();
    }

    delete serial;
    // Set _working to false, meaning the process can't be aborted anymore.
    mutex.lock();
    _working = false;
    mutex.unlock();

    qDebug() << "Server Worker process finished in Thread " << thread()->currentThreadId();

    emit finished();
}

void LocalServerWorker::updateRCValues(QString msg)
{
    if (msg.length() > 0)
    {
        QStringList msgFields = msg.split(":");
        if (msgFields.at(0) == "thr")
        {
            int tempValue = msgFields.at(1).toInt();
            if ( (tempValue >= 1000) && (tempValue <= 2000) )
            {
                manual_rc_values.rcData[2] = tempValue;
            }
        }
        else if (msgFields.at(0) == "yaw")
        {
            int tempValue = msgFields.at(1).toFloat();
            if ( (tempValue >= -180.0) && (tempValue <= 180.0) )
            {
                manual_rc_values.rcData[3] = mapAngleToPWM(tempValue, -180.0, 180.0, 1000, 2000);
            }
        }
        else if (msgFields.at(0) == "pitch")
        {
            int tempValue = msgFields.at(1).toFloat();
            if ( (tempValue >= -90.0) && (tempValue <= 90.0) )
            {
                manual_rc_values.rcData[1] = mapAngleToPWM(tempValue, -90.0, 90.0, 1000, 2000);
            }
        }
        else if (msgFields.at(0) == "roll")
        {
            int tempValue = msgFields.at(1).toFloat();
            if ( (tempValue >= -90.0) && (tempValue <= 90.0) )
            {
                manual_rc_values.rcData[0] = mapAngleToPWM(tempValue, -90.0, 90.0, 1000, 2000);
            }
        }
        else if (msgFields.at(0) == "aux1")
        {
            int tempValue = msgFields.at(1).toInt();
            if (tempValue == 1)
            {
                manual_rc_values.rcData[4] = 1350;
            }
            else if (tempValue == 0)
            {
                manual_rc_values.rcData[4] = 1000;
            }
        }
        else if (msgFields.at(0) == "aux2")
        {
            int tempValue = msgFields.at(1).toInt();
            if (tempValue == 1)
            {
                manual_rc_values.rcData[5] = 1000;
            }
            else if (tempValue == 0)
            {
                manual_rc_values.rcData[5] = 1100;
            }
        }
        else if (msgFields.at(0) == "aux3")
        {
            int tempValue = msgFields.at(1).toInt();
            if (tempValue == 1)
            {
                manual_rc_values.rcData[6] = 1800;
            }
            else if (tempValue == 0)
            {
                manual_rc_values.rcData[6] = 1000;
            }
        }
        else if (msgFields.at(0) == "aux4")
        {
            int tempValue = msgFields.at(1).toInt();
            if (tempValue == 1)
            {
                manual_rc_values.rcData[7] = 1800;
            }
            else if (tempValue == 0)
            {
                manual_rc_values.rcData[7] = 1000;
            }
        }
    }
}

uint16_t LocalServerWorker::mapAngleToPWM(float realAngle, float minAngle, float maxAngle, uint16_t minPWM, uint16_t maxPWM)
{
    uint16_t realPWM = int((maxPWM - minPWM)*(realAngle - minAngle)/(maxAngle - minAngle) + minPWM);
    return realPWM;
}

void LocalServerWorker::realWorker()
{
    if (serialReady == true)
    {
        switch (_manualMode) {
        case 0:
        {
            break;
        }
        case 1:
        {
            qDebug() << manual_rc_values.rcData[0]
                     << manual_rc_values.rcData[1]
                     << manual_rc_values.rcData[2]
                     << manual_rc_values.rcData[3]
                     << manual_rc_values.rcData[4]
                     << manual_rc_values.rcData[5]
                     << manual_rc_values.rcData[6]
                     << manual_rc_values.rcData[7];
            sc_xbee_at->sendCMD(MSP_SET_RAW_RC, manual_rc_values);
            break;
        }
        case 2:
        {
            break;
        }
        case 3:
        {
            break;
        }
        default:
            break;
        }

        QTime dieTime1= QTime::currentTime().addMSecs(50);
        while( QTime::currentTime() < dieTime1 )
        {
            QEventLoop loop;
            QTimer::singleShot(1, &loop, SLOT(quit()));
            loop.exec();
        }
    }
}

QString LocalServerWorker::get_serialPortName() const
{
    return _serialPortName;
}

void LocalServerWorker::set_serialPortName(QString value)
{
    _serialPortName = value;
    //qDebug() << "set serial port name" << _serialPortName;
}

bool LocalServerWorker::get_auxSerialOn() const
{
    return _auxSerialOn;
}

void LocalServerWorker::set_auxSerialOn(bool value)
{
    _auxSerialOn = value;
    if (_auxSerialOn == true)
    {
        serial = new QSerialPort;
        //qDebug() << _serialPortName;
        serial->setPortName(_serialPortName);
    }
    else if (_auxSerialOn == false)
    {
        if (serial->isOpen())
        {
            serial->close();
        }
        delete serial;
    }
}

int LocalServerWorker::get_manualMode() const
{
    return _manualMode;
}

void LocalServerWorker::set_manualMode(int value)
{
    _manualMode = value;
    if ((_auxSerialOn == true) && (_manualMode > 0))
    {
        QuadStates *tempQS;
        tempQS = new QuadStates(QByteArray::fromHex("1"),
                                QByteArray::fromHex("0013a20040c14306"),
                                QByteArray::fromHex("fffe"));
        quadstates_list.append(tempQS);
        sc_xbee_at = new SerialCommunication_XBEE_AT(serial, quadstates_list);
        serialReady = true;
    }
    else
    {
        serialReady = false;
        if (_manualMode == 0)
        {
            if (serial->isOpen())
            {
                serial->close();
            }
        }
    }
}
