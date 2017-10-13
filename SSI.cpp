#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <memory>

#include "KleinSonar.h"

static bool shutdown = false;

#include <errno.h>
#include <string.h>

#include <sys/time.h> // gettimeofday

// a funtion to print the curent time to ostream
static std::ostream& printTime(std::ostream& os)
{
	static char buf[255];
	static const char format[] = "%X";
	struct timeval tv;
	struct timezone tz;

	// ignore return value
	(void) gettimeofday(&tv, &tz);

	const struct tm* const tmp = localtime(&tv.tv_sec);
	
	if (tmp == NULL)
	{
		throw strerror(errno);
	}

	if (strftime(buf, sizeof(buf), format, tmp) == 0)
		throw "strftime failure";

	os << buf << "." << std::setw(3) << std::setfill('0') << (int)(tv.tv_usec/ 1e3);
	return os;
}

class SPUStatusInterface 
{

public:

	SPUStatusInterface(const std::string& spu, const bool& useBlockingSockets) : 
		m_tpuHandle(NULL), m_spuIP(spu), m_useBlockingSockets(useBlockingSockets) {}

	virtual ~SPUStatusInterface(void)
	{
		if (m_tpuHandle);
		{
			DllCloseTheTpu(m_tpuHandle);
		}
	}

	const int execute()
	{
		connectToTPU();

		BoolStat tpuStatus  = NGS_FAILURE;
		int lastPingNum = -1;

		while (!shutdown)
		{
			U32 pageStatus = NGS_FAILURE;
			CKleinType3Header headerInfo;
			memset((void*)&headerInfo, 0, sizeof(CKleinType3Header));

			tpuStatus = DllGetTheTpuDataHeader3(m_tpuHandle, lastPingNum + 1, &pageStatus, &headerInfo);

			if (tpuStatus != NGS_SUCCESS)
			{
				// clean up TPU Handle
				disconnectFromTPU();
				// get a new handle and reset last ping number
				connectToTPU();
				lastPingNum = -1;
				continue;	// while
			}

			if (pageStatus == (U32)NGS_GETDATA_SUCCESS)
			{
				printTime(std::cout);
				std::cout 
					<< " - Ping : " << headerInfo.pingNumber
					<< ", Error: 0x" << std::hex << std::setfill('0') << std::setw(8) << headerInfo.errorFlags
					<< ", Pitch: " << std::dec << std::setfill(' ') << headerInfo.pitch
					<< ", Roll: " << std::dec << std::setfill(' ') << headerInfo.roll
					<< ", Altitude: " << std::dec << std::setfill(' ') << headerInfo.altitude
					<< std::endl;

				lastPingNum = headerInfo.pingNumber;

				usleep(1e5);
			}
			else if (pageStatus == (U32)NGS_GETDATA_ERROR_OLD)
			{
				lastPingNum = -1;	//ask for the latest ping next
				std::cerr << "Page Status (NGS_GETDATA_ERROR_OLD): " << ((int)pageStatus) << std::endl;
				usleep(1e5);
			}
			else
			{
				// nothing to do - no pages yet available
				usleep(1e5);
			}
		}

		disconnectFromTPU();

		return 0;
	}

private:

	void connectToTPU()
	{
		DLLErrorCode errorCode = NGS_NO_CONNECTION_WITH_TPU;

		while (errorCode != NGS_NO_ERROR && errorCode != NGS_ALREADY_CONNECTED && !shutdown)
		{
			U32 protocolVersion = 0;

			if (m_useBlockingSockets)
			{
				// cast away const :(
				m_tpuHandle = DllOpenTheTpu(0, (char *)m_spuIP.c_str(), &protocolVersion);
			}
			else
			{
				static const U32 connectTimeoutMs = 250;
				m_tpuHandle = DllOpenTheTpuNonBlocking(0, (char *)m_spuIP.c_str(), connectTimeoutMs, &protocolVersion);
			}

			DllGetLastError(m_tpuHandle, &errorCode);

			if ( errorCode == NGS_NO_ERROR )
			{
				break; // while
			}
			else if (errorCode == NGS_ALREADY_CONNECTED )
			{
				break; // while
			}
			else if (errorCode == NGS_NO_CONNECTION_WITH_TPU)
			{
				printTime(std::cerr);
				std::cerr << " - No connection with TPU " << std::endl; 
				// need to free the TPUHandle on failure
				disconnectFromTPU(); 
			}
			else
			{
				printTime(std::cerr);
				std::cerr << " - Error code: " << errorCode << std::endl;
				// need to free the TPUHandle on failure
				disconnectFromTPU();
			}

			{ printTime(std::cerr); std::cerr << " - Alarm raised " << std::endl; usleep(1e6); }
		}

		{ printTime(std::cerr); std::cerr << " - Alarm lowered " << std::endl; usleep(1e6); }

	}

	void disconnectFromTPU()
	{
		if (m_tpuHandle == NULL)	//Nothing to do, already disconnected
			return;

		try
		{
			DllCloseTheTpu(m_tpuHandle);
		}
		catch (...)
		{
			std::cerr << "Connection to the TPU was not properly closed." << std::endl;
		}

		m_tpuHandle = NULL;	
	}

	TPU_HANDLE m_tpuHandle;
	const std::string m_spuIP;
	const bool m_useBlockingSockets;
};


// ^C handler
static void terminate(const int param)
{
	if (shutdown)
	{
		std::cerr << "shutdown failed" << std::endl;
		exit(-1);
	}
	shutdown = true;
}

int main(const int ac, const char* const av[])
{
	// map ^C handler
 	(void) signal(SIGINT, terminate);

	const std::string spu("192.168.0.81");
	/// const std::string spu("k007");

	const bool useBlockingSockets = true;
	/// const bool useBlockingSockets = false;

	std::cout << "Using blocking sockets: " << std::boolalpha << useBlockingSockets 
		<< " with SPU: " <<  spu << std::endl;

	SPUStatusInterface SSI(spu, useBlockingSockets);

	std::cout << "SSI.execute returns: " << SSI.execute() << std::endl;

	return 0;
}


