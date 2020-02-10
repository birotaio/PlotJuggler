#include "ControllerDataStream.h"
#include <QMessageBox>
#include <thread>
#include <mutex>
#include <chrono>
#include <fcntl.h>   /* File Control Definitions           */
#include <termios.h> /* POSIX Terminal Control Definitions */
#include <unistd.h>  /* UNIX Standard Definitions      */
#include <cstdint>
#include <QInputDialog>
#include <QStringList>
#include <QDir>
#include <QDate>
#include <QTime>
#include "stdio.h"

#define ASSERV_FREQ 2116.0
#define DEFAULT_OUTPUT_FILENAME "data_"
#define DEFAULT_EXTENSION ".csv"

ControllerStream::ControllerStream():_running(false),fd(-1)
{
	QStringList  words_list;
	words_list
			<< "timestamp"
			<<"torque"
			<<"wheel speed (cRPM)"
			<<"pedal speed (cRPM)";

	foreach( const QString& name, words_list)
	dataMap().addNumeric(name.toStdString());
	_fileInitialized = false;
}

bool ControllerStream::openPort()
{
	bool ok;

	QDir devDir("/dev/");
	QStringList ttyListAbsolutePath;

	QFileInfoList list = devDir.entryInfoList(QStringList() << "ttyUSB*" ,QDir::System);
	for (int i = 0; i < list.size(); i++)
		ttyListAbsolutePath << list.at(i).absoluteFilePath();

	QString device = QInputDialog::getItem(nullptr,
										   tr("Which tty use ?"),
										   tr("/dev/tty?"),
										   ttyListAbsolutePath,
										   0, false, &ok);

	std::string ttyName = device.toStdString();


	if(!ok)
		return false;

	fd = open(ttyName.c_str(),O_RDWR | O_NOCTTY);
	if( fd == -1)
	{
		printf("Unable to open %s\n",ttyName.c_str());
		return false;
	}
	else
	{
		printf("Port %s opened\n",ttyName.c_str());
		return true;
	}
}

bool ControllerStream::start(QStringList*)
{
	bool ok = openPort();
	bool tryAgain = !ok;
	while(tryAgain)
	{
		QMessageBox::StandardButton reply =
				QMessageBox::question(nullptr, "Unable to open port", "Unable to open port\nTry again?",
									  QMessageBox::Yes|QMessageBox::No);
		if (reply == QMessageBox::No)
		{
			tryAgain = false;
		}
		else
		{
			ok = openPort();
			tryAgain = !ok;
		}
	}

	QMessageBox::StandardButton reply =
			QMessageBox::question(nullptr, "Save data to file", "Would you like to save the plotted data?",
								  QMessageBox::Yes|QMessageBox::No);
	_saveData = (reply == QMessageBox::Yes);
	if (_saveData){
		_fileInitialized = false;
	}

	if(ok)
	{

		// Insert dummy Point. it seems that there's a regression on plotjuggler, this dirty hack seems now necessary
		for (auto& it: dataMap().numeric )
		{
			auto& plot = it.second;
			plot.pushBack( PlotData::Point( 0, 0 ) );
		}
		_running = true;
		_thread = std::thread([this](){ this->loop();} );
		return true;
	}
	else
	{
		return false;
	}
}

void ControllerStream::shutdown()
{
	_running = false;

	if(fd != -1)
		close(fd);

	if( _thread.joinable()) _thread.join();
}

bool ControllerStream::isRunning() const { return _running; }

ControllerStream::~ControllerStream()
{
	shutdown();
}

void ControllerStream::pushSingleCycle()
{
	if (!_fileInitialized && _saveData){
		updateFilename(DEFAULT_OUTPUT_FILENAME);
		QFile file(_outputFileName);
		file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text);
		QString finalString;
		for (auto& it: dataMap().numeric ){
			finalString.append(QString::fromStdString(it.first));
			finalString.append(",");
		}
		int stringSize = finalString.size();
		finalString = finalString.left(stringSize - 1);
		finalString.append("\n");
		file.write(finalString.toLatin1().data());
		file.close();
		_fileInitialized = true;
	}

	StreamSample sample;
	while( uartDecoder.getDecodedSample(&sample) )
	{
		std::lock_guard<std::mutex> lock( mutex() );

		double timestamp = double(sample.timestamp) * 1.0/ASSERV_FREQ;
		QFile file(_outputFileName);
		file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text);
		QString finalString;
		for (auto& it: dataMap().numeric )
		{
			double value = getValueFromName(it.first, sample);
			if(value == NAN)
				value = 0;
			auto& plot = it.second;
			plot.pushBack( PlotData::Point( timestamp, value ) );
			finalString.append(QString::number(value, 'f'));
			finalString.append(",");
		}
		int stringSize = finalString.size();
		finalString = finalString.left(stringSize - 1);
		finalString.append("\n");
		file.write(finalString.toLatin1().data());
		file.close();
	}
}

void ControllerStream::loop()
{
	_running = true;
	while( _running  && fd != -1)
	{
		uint8_t read_buffer[sizeof(StreamSample)+4];
		unsigned int  bytes_read = 0;
		bytes_read = read(fd,read_buffer,sizeof(read_buffer));

		if(bytes_read > 0)
			uartDecoder.processBytes(read_buffer,bytes_read );

		pushSingleCycle();
		std::this_thread::sleep_for( std::chrono::microseconds(400) );
	}

}

double ControllerStream::getValueFromName(const  std::string &name, StreamSample &sample)
{
	double value = 0;

	if( name == "timestamp")
		value = sample.timestamp;
	else if( name == "torque")
		value = sample.torque;
	else if( name == "wheel speed (cRPM)")
		value = sample.wheelSpeed;
	else if( name == "pedal speed (cRPM)")
		value = sample.pedalSpeed;

	return value;
}

void ControllerStream::updateFilename(QString basename){
	QDate today = QDate::currentDate();
	QTime now = QTime::currentTime();
	QString day = QString::number(today.day());
	QString month = QString::number(today.month());
	QString year = QString::number(today.year());
	QString hour = QString::number(now.hour());
	QString minutes = QString::number(now.minute());
	QString seconds = QString::number(now.second());
	_outputFileName = basename;
	_outputFileName.append(day);
	_outputFileName.append(month);
	_outputFileName.append(year);
	_outputFileName.append(hour);
	_outputFileName.append(minutes);
	_outputFileName.append(seconds);
	_outputFileName.append(DEFAULT_EXTENSION);

	QByteArray byteArray = _outputFileName.toLocal8Bit();
	printf("Filename: %s\n",byteArray.data());
}

bool ControllerStream::xmlSaveState(QDomDocument &doc, QDomElement &parent_element) const
{
	return true;
}

bool ControllerStream::xmlLoadState(const QDomElement &parent_element)
{
	return false;
}