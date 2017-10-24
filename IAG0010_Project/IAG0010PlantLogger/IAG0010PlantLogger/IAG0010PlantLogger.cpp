#include "stdafx.h"
#include "Winsock2.h" // necessary for sockets, Windows.h is not needed.
#include "mswsock.h"
#include "IAG0010PlantLogger.h"
#include "process.h"
#include "time.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

HANDLE file; //File where we will write the received data
TCHAR * output_file = L"output.txt"; //name and/or path of the txt file to be written

			 //Socket variables
WSADATA wsadata; //Windows sockets initialization
DWORD Error;
SOCKET hClientSocket = INVALID_SOCKET;
sockaddr_in ClientSocketInfo;
BOOL SocketError;
BOOL sendConnectionNotAccepted = TRUE;
BOOL connected = FALSE;

//Events
HANDLE hCommandGot;			// "User typed a command"
HANDLE hStopCommandGot;		// "Stop command recognized by the Main thread"
HANDLE hCommandProcessed;	// "Main thread finished processing a command"
HANDLE hConnectCommandGot;	// "Connexion requested"
HANDLE hSendPassword;		// "Emulator requesting password"
HANDLE hSend;				// "Data has been sent"

HANDLE hReadKeyboard;		// Keyboard reading thread handle
HANDLE hStdIn;				// Standard input stream handle
HANDLE hReceiveNet;			//TCP/IP info receiving thread
HANDLE hSendNet;			//TCP/IP info sending thread

							//Variables for receiving thread
WSAOVERLAPPED receiveOverlapped;
HANDLE WSAReceiveCompletedEvents[2];
HANDLE ReceivedEvents[2];


TCHAR CommandBuf[81];		//Buffer for the command written by the user
BOOL startOK = FALSE;
// Prototypes of our threads
unsigned int __stdcall ReadKeyboard(void* pArguments);
unsigned int __stdcall ReceiveNet(void* pArguments);
unsigned int __stdcall SendNet(void* pArguments);

int writeToFile(char * data, HANDLE file); //will write the data received in a .txt file
const char * displayAndWrite(char *data); //reads the received data, displays in the console, and writes it to a file

//****************************************************************************************************************
//                                 MAIN THREAD
//****************************************************************************************************************
int _tmain(int argc, _TCHAR* argv[])
{
	//
	// Initializations for multithreading
	//
	if (!(hCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hStopCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hCommandProcessed = CreateEvent(NULL, TRUE, TRUE, NULL)) ||
		!(hSend = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hConnectCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)))
	{
		_tprintf(_T("CreateEvent() failed, error %d\n"), GetLastError());
		return 1;
	}

	WSAReceiveCompletedEvents[0] = hStopCommandGot;
	ReceivedEvents[0] = hStopCommandGot;
	memset(&receiveOverlapped, 0, sizeof receiveOverlapped);
	receiveOverlapped.hEvent = WSAReceiveCompletedEvents[1] = WSACreateEvent();
	ReceivedEvents[1] = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (!WSAReceiveCompletedEvents[1] || !ReceivedEvents[1]) {
		_tprintf(_T("CreateEvent() failed for network events, error %d\n"), GetLastError());
		goto out_main;
	}

	//
	// Prepare keyboard and starts ReadKeyboard thread
	//
	hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdIn == INVALID_HANDLE_VALUE) {
		_tprintf(_T("GetStdHandle() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!SetConsoleMode(hStdIn, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) {
		_tprintf(_T("SetConsoleMode() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!(hReadKeyboard = (HANDLE)_beginthreadex(NULL, 0, &ReadKeyboard, NULL, 0, NULL))) {
		_tprintf(_T("Unable to create keyboard thread\n"));
		return 1;
	}

	//
	// Socket initialization
	//
	if (Error = WSAStartup(MAKEWORD(2, 0), &wsadata)) {
		_tprintf(_T("WSAStartup failed, error %d\n"), Error);
		SocketError = TRUE;
	}

	else if ((hClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
		_tprintf(_T("socket() failed, error %d\n"), WSAGetLastError());
		SocketError = TRUE;
	}

	//
	// Connecting client to the server
	//
	if (SocketError) {
		_tprintf(_T("We have an error with socket: %d\n"), WSAGetLastError());
	}
	if (!SocketError) {
		ClientSocketInfo.sin_family = AF_INET;
		ClientSocketInfo.sin_addr.s_addr = inet_addr("127.0.0.1");
		ClientSocketInfo.sin_port = htons(1234);

		if (connect(hClientSocket, (SOCKADDR*)&ClientSocketInfo, sizeof(ClientSocketInfo)) == SOCKET_ERROR) {
			_tprintf(_T("Unable to connect  the server, error %d\n"), WSAGetLastError());
			SocketError = TRUE;
		}
	}

	//
	// Start ReceiveNet and SendNet threads
	//
	if (!SocketError) {
		if (!(hReceiveNet = (HANDLE)_beginthreadex(NULL, 0, &ReceiveNet, NULL, 0, NULL))) {
			_tprintf(_T("Unable to create socket receiving thread\n"));
			goto out_main;
		}
		if (!(hReceiveNet = (HANDLE)_beginthreadex(NULL, 0, &SendNet, NULL, 0, NULL))) {
			_tprintf(_T("Unable to create socket sending thread\n"));
			goto out_main;
		}
	}

	while (TRUE) {
		if (WaitForSingleObject(hCommandGot, INFINITE) != WAIT_OBJECT_0) {
			_tprintf(_T("WaitForSingleObject() failed, error %d\n"), GetLastError());
			goto out_main;
		}
		ResetEvent(hCommandGot);
		
		if (!connected) {
			if (!_tcsicmp(CommandBuf, _T("connect"))) { //used for test
				SetEvent(hCommandProcessed); //keyboard thread can continue working
				//return _tmain(argc, argv);
			}			
			else if (!_tcsicmp(CommandBuf, _T("exit"))) { //user typed "exit" command
				_tprintf(_T("Terminating...\n"));
				SetEvent(hStopCommandGot);
				break;
			}
			else {
				SetEvent(hCommandProcessed); //keyboard thread can continue working
				SetEvent(hReadKeyboard);
				SetEvent(hSend);
			}
		}
		else {
			if (!_tcsicmp(CommandBuf, _T("exit"))) { //user typed "exit" command
				_tprintf(_T("Terminating...\n"));
				SetEvent(hStopCommandGot);
				break;
			}
			
			else if (!_tcsicmp(CommandBuf, _T("start"))) { //used for test
				_tprintf(_T("Command start accepted.\n"));
				SetEvent(hCommandProcessed); //keyboard thread can continue working
				wcscpy(CommandBuf, _T("Start"));
				startOK = TRUE;
				SetEvent(hSend);	
			}

			else if (!_tcsicmp(CommandBuf, _T("break"))) { //used for test
				_tprintf(_T("Command break accepted.\n"));
				wcscpy(CommandBuf, _T("Break"));
				SetEvent(hCommandProcessed); //keyboard thread can continue working
				SetEvent(hSend);
			}

			else if (!_tcsicmp(CommandBuf, _T("stop"))) { //used for test
				_tprintf(_T("Command stop accepted.\n"));
				SetEvent(hCommandProcessed); //keyboard thread can continue working
				wcscpy(CommandBuf, _T("Stop"));
				SetEvent(hSend);
				sendConnectionNotAccepted = FALSE;
				connected = FALSE;
				startOK = FALSE;
			}

			else {
				_tprintf(_T("The command is not recognized.\n"));
				SetEvent(hCommandProcessed);
			}
			
		//if (!_tcsicmp(CommandBuf, _T("exit"))) { //user typed "exit" command
		/*	_tprintf(_T("Terminating...\n"));
			SetEvent(hStopCommandGot);
			break;

		}*/

		//else if ((!_tcsicmp(CommandBuf, _T("connect")))/*&&(!connectOK)*/) {
		/*	_tprintf(_T("Command Connect accepted\n"));
			wcscpy(CommandBuf, _T("coursework"));
			SetEvent(hConnectCommandGot); //user requested to connect
			SetEvent(hCommandProcessed); //keyboard thread can continue working
			SetEvent(hSend);
		}

		else if (!_tcsicmp(CommandBuf, _T("start"))) {
			_tprintf(_T("Command Start accepted.\n"));
			wcscpy(CommandBuf, _T("Start"));
			SetEvent(hCommandProcessed); //keyboard thread can continue working
			SetEvent(hSend);
			startOK = TRUE;
		}

		else if (!_tcsicmp(CommandBuf, _T("break"))) {
			_tprintf(_T("Command Break accepted.\n"));
			wcscpy(CommandBuf, _T("Break"));
			SetEvent(hCommandProcessed); //keyboard thread can continue working
			SetEvent(hSend);
		}

		else if (!_tcsicmp(CommandBuf, _T("ready"))) {
			_tprintf(_T("Command Ready accepted.\n"));
			wcscpy(CommandBuf, _T("Ready"));
			SetEvent(hCommandProcessed); //keyboard thread can continue working
			SetEvent(hSend);
		}

		else {
			_tprintf(_T("Command \"%s\" not recognized\n"), CommandBuf);
			SetEvent(hCommandProcessed); //keyboard thread can continue working*/
		}
	}

	// Shut down
out_main:
	if (hReadKeyboard) {
		WaitForSingleObject(hReadKeyboard, INFINITE);
		CloseHandle(hReadKeyboard);
	}

	if (hReceiveNet) {
		WaitForSingleObject(hReceiveNet, INFINITE);
		CloseHandle(hReceiveNet);
	}
	if (hClientSocket != INVALID_SOCKET) {
		if (shutdown(hClientSocket, SD_RECEIVE) == SOCKET_ERROR) {
			if ((Error = WSAGetLastError()) != WSAENOTCONN)
				_tprintf(_T("shutdown() failed, error %d\n"), WSAGetLastError());
		}
		closesocket(hClientSocket);
	}
	WSACleanup();
	CloseHandle(hStopCommandGot);
	CloseHandle(hCommandGot);
	CloseHandle(hCommandProcessed);
	return 0;
}

//**************************************************************************************************************
//                          KEYBOARD READING THREAD
//**************************************************************************************************************
unsigned int __stdcall ReadKeyboard(void* pArguments) {
	DWORD nReadChars;
	HANDLE KeyboardEvents[2];
	KeyboardEvents[1] = hCommandProcessed;
	KeyboardEvents[0] = hStopCommandGot;
	DWORD WaitResult;

	// Reading loop
	while (TRUE) {
		WaitResult = WaitForMultipleObjects(2, KeyboardEvents, FALSE, INFINITE);
		if (WaitResult == WAIT_OBJECT_0)
			return 0; // Stop command got
		else if (WaitResult == WAIT_OBJECT_0 + 1) { // command processed
			_tprintf(_T("Insert command\n"));
			if (!ReadConsole(hStdIn, CommandBuf, 80, &nReadChars, NULL)) {
				_tprintf(_T("ReadConsole() failed, error %d\n"), GetLastError());
				return 1;
			}
			printf("\n");
			CommandBuf[nReadChars - 2] = 0; // to get rid of \r\n
			ResetEvent(hCommandProcessed); //hCommandProcessed to non-signaled
			SetEvent(hCommandGot); // hCommandGot event to signaled
		}
		else { // waiting failed
			_tprintf(_T("WaitForMultipleObjects() failed, error %d\n"), GetLastError());
			return 1;
		}
	}
	return 0;
}

//********************************************************************************************************************
//                          TCP/IP INFO RECEIVING THREAD
//********************************************************************************************************************
unsigned int __stdcall ReceiveNet(void* pArguments) {

	WSABUF DataBuf; // Buffer for received data is a structure
	char ArrayInBuf[2048];
	DataBuf.buf = &ArrayInBuf[0];
	DataBuf.len = 2048;
	DWORD nReceivedBytes = 0; // Pointer to the number, in bytes, of data received by this call
	DWORD ReceiveFlags = 0; // Pointer to flags used to modify the behaviour of the WSARecv function call
	HANDLE NetEvents[2];
	NetEvents[0] = hStopCommandGot;
	WSAOVERLAPPED Overlapped;
	memset(&Overlapped, 0, sizeof(Overlapped));
	Overlapped.hEvent = NetEvents[1] = WSACreateEvent();
	DWORD Result, Error;
	wchar_t *identifier = L"Identify";
	wchar_t *accepted = L"Accepted";
	wchar_t *notAccepted = L"Not accepted";
	wchar_t *DataPointer;
	int n = 0;

	//
	// Initialization of the written file
	//
	DWORD nBytesToWrite, nBytesWritten = 0, nBytesToRead, nReadBytes = 0;
	BYTE *pBuffer;
	file = CreateFile(output_file, // TCHAR *
		GENERIC_READ | GENERIC_WRITE,
		0, NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);

	if (file == INVALID_HANDLE_VALUE) //gestion des erreurs
		_tprintf(_T("File not created, error %d"), GetLastError());


	//
	// Receiving loop
	//
	while (TRUE) {
		Result = WSARecv(hClientSocket, &DataBuf, 1, &nReceivedBytes, &ReceiveFlags, &Overlapped, NULL);
		if (Result == SOCKET_ERROR) {

			if (Error = WSAGetLastError() != WSA_IO_PENDING) {// Unable to continue
				_tprintf(_T("WSARecv() failed, error %d\n"), Error);
				goto out_receive;
			}

			DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents, FALSE, WSA_INFINITE, FALSE); //wait for received data

			switch (WaitResult) {
			case WAIT_OBJECT_0:		//hStopCommandGot signaled, user wants to exit

				goto out_receive;

			case WAIT_OBJECT_0 + 1: //Overlapped.hEvent signaled, received operation is over
				WSAResetEvent(NetEvents[1]); //To be ready for the next data package

				_tprintf(_T("Message received from emulator.\n"));

				if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nReceivedBytes, FALSE, &ReceiveFlags)) {
					int i = 0;

					_tprintf(_T("%d bytes received\n"), nReceivedBytes);
					printf("Message received is: ");

					if (startOK == TRUE) {
						displayAndWrite(&ArrayInBuf[0]);

					}



					else if (sendConnectionNotAccepted) {
						for (i = 4; i <= (int)nReceivedBytes - 2; i = i + 2) {
							printf("%c", ArrayInBuf[i]);
						}
						printf("\n");

						DataPointer = (wchar_t*)(DataBuf.buf + 4);

						if (!wcscmp(DataPointer, identifier)) //We have received an identification request from the Emulator
						{
							_tprintf(_T("Identification requested...\n"));
							SetEvent(hSendPassword);
						}
						
						if (!wcscmp(DataPointer, accepted)) //Good password
							connected = TRUE;
												
						if (!wcscmp(DataPointer, notAccepted)) //wrong password
							Sleep(5);
					}
					break;
				}
				else {// Fatal problems
					_tprintf(_T("WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
					goto out_receive;
				}
			default: // Fatal problems
				_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
				goto out_receive;
			}
		}
		else {
			if (!nReceivedBytes) {
				_tprintf(_T("Server has closed the connection\n"));
				goto out_receive;
			}
			else {
				_tprintf(_T("%d bytes received\n"), nReceivedBytes);
				// Here should follow the processing of received data
			}
		}
	}
out_receive:
	WSACloseEvent(NetEvents[1]);
	return 0;
}

//********************************************************************************************************************
//                          TCP/IP INFO SENDING THREAD
//********************************************************************************************************************
unsigned int __stdcall SendNet(void* pArguments) {

	// Initialization
	char message[50] = "  coursework";
	WSABUF sendDataBuf; // Buffer for sent data
	sendDataBuf.buf = &message[0];                         
	sendDataBuf.len = 2048;
	CHAR data[2044];
	int dataLength = 0;

	DWORD nSendBytes = 0; // Pointer to the number, in bytes, of data sent by this call
	DWORD SendFlags = 0; // Pointer to flags used to modify the behaviour of the WSASend function call
	DWORD waitResult = 0;

	//Variables for sending thread
	HANDLE SentEvents[3];
	SentEvents[0] = hStopCommandGot;
	WSAOVERLAPPED sendOverlapped;
	memset(&sendOverlapped, 0, sizeof sendOverlapped);
	sendOverlapped.hEvent = SentEvents[1] = WSACreateEvent();
	SentEvents[2] = hSend;
	DWORD Error, Result;

	while (TRUE) {
		waitResult = WSAWaitForMultipleEvents(3, SentEvents, FALSE, WSA_INFINITE, FALSE); //Waiting for data
		switch (waitResult) {

		case WAIT_OBJECT_0:
			//Stop has been signaled by user
			goto out_send;

		case WAIT_OBJECT_0 + 1:
			WSAResetEvent(SentEvents[1]);
			if (Result == SOCKET_ERROR) {
				if ((Error = WSAGetLastError()) != WSA_IO_PENDING) {
					_tprintf(_T("Sending thread WSASend() failed, error %d\n"), Error);
					goto out_send;
				}

				else { // Fatal problems
					_tprintf(_T("Sending thread WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
					goto out_send;
				}
			}

		case WAIT_OBJECT_0 + 2:

			strcpy(data, (char *)CommandBuf);
			_tprintf(_T("Sending : %s\n"), (wchar_t *)CommandBuf);
			//dataLength = strlen((char *)CommandBuf);
			dataLength = wcslen(CommandBuf) * 2 + 6;
			_tprintf(_T("size is: %d\n"), dataLength);
			memcpy(sendDataBuf.buf, &dataLength, 4);
			sendDataBuf.buf[0] = dataLength;
			memcpy(sendDataBuf.buf + 4, (char *)CommandBuf, 5 * dataLength);

			sendDataBuf.len = wcslen(CommandBuf) * 2 + 6;
			nSendBytes = sendDataBuf.len;

			//nSendBytes = (sizeof(sendDataBuf.buf)) - 1;
			Result = WSASend(hClientSocket, &sendDataBuf, 1, &nSendBytes, SendFlags, &sendOverlapped, NULL);


			break;

		default: // Fatal problems
			_tprintf(_T("Sending thread WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
			goto out_send;
		}
		WSAResetEvent(SentEvents[1]);
		WSAResetEvent(SentEvents[2]);
	}
out_send:
	_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
	WSACloseEvent(SentEvents[1]);
	return 0;
}

int writeToFile(char * dataToWrite, HANDLE file) {
	int length = strlen(dataToWrite); //length of the string to write
	DWORD nBytesWritten; //number of bytes written 

	if (!WriteFile(file, dataToWrite, length * sizeof(char), &nBytesWritten, NULL)) {
		_tprintf(_T("Unable to write to file. Received error %d"), GetLastError());
		return 1;
	}
	if (nBytesWritten != length) {
		_tprintf(_T("Write failed, only %d bytes written\n"), nBytesWritten);
	}
	return 0;
}

const char * displayAndWrite(char *data) {
	int position = 0; //positionition in the data package

	int length;
	memcpy(&length, data, sizeof(int)); //we specify sizeof(int) to only select the first information in the package
	position = sizeof(int); //our position 
	_tprintf(_T("Number of bytes in package: %d\n"), length);

	int channels_number; //number of channels in the data package
	memcpy(&channels_number, data + position, sizeof(int));
	position = position + sizeof(int);
	_tprintf(_T("Number of channels in package: %d\n"), channels_number);

	char dataToWrite[2048]; //data that needs to be written in the file

	time_t timeNow = time(NULL);
	struct tm *t = localtime(&timeNow);
	char currentTime[2048];
	sprintf(currentTime, "Measurements at %d-%02d-%02d %02d:%02d:%02d",
		t->tm_year + 1900, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	printf(currentTime);
	strcpy(dataToWrite, currentTime);	//we copy the strings about time to the data needed to be written in the file
	strcat(dataToWrite, "\n");			//and concatenate a \n for a proper presentation
	writeToFile(dataToWrite, file); //data is written to our file

	for (int i = 0; i < channels_number; i++) { //we repeat the procedure for each channel
		int pointsNumber; //number of measurement points in the active channel

		memcpy(&pointsNumber, data + position, sizeof(int));
		position = position + sizeof(int);
		_tprintf(_T("Number of measurement points for the active channel: %d\n"), pointsNumber);

		char channelName[2048]; //name of the active channel
		memccpy(channelName, data + position, '\0', 15);
		printf("Channel name: %s\n", channelName);
		strcpy(dataToWrite, channelName);
		strcat(dataToWrite, ":\n");
		writeToFile(dataToWrite, file);
		position = position + strlen(channelName) + 1;

		for (int j = 0; j < pointsNumber; j++) { //we repeat the procedure for each point
			char pointName[2048]; //name of the active point
			memccpy(pointName, data + position, '\0', 35);
			printf("Point name: %s\n", pointName);
			strcpy(dataToWrite, pointName);
			strcat(dataToWrite, ": ");
			writeToFile(dataToWrite, file);
			position = position + strlen(pointName) + 1;

			char stringMeasurements[2048]; //data that contains numerical values

			if (!strcmp(pointName, "Input solution flow") ||
				!strcmp(pointName, "Input liquid flow") ||
				!strcmp(pointName, "Output solution flow") ||
				!strcmp(pointName, "Output liquid flow")) {

				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T("Measurement: %.3f m%c/s\n"), measurement, 252);
				sprintf(stringMeasurements, "%.3f m^3/s\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Input solution temperature")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T("Measurement: %.1f %cC\n"), measurement, 248);
				sprintf(stringMeasurements, "%.1f %cC\n", measurement, 248);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Input solution pressure") ||
				!strcmp(pointName, "Input air pressure")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T("Measurement: %.1f atm\n"), measurement);
				sprintf(stringMeasurements, "%.1f atm\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Extracted product concentration") ||
				!strcmp(pointName, "Disulfid on output")) {
				int measurement;
				memcpy(&measurement, data + position, sizeof(int));
				position = position + sizeof(int);
				_tprintf(_T("Measurement: %d %%\n"), measurement);
				sprintf(stringMeasurements, "%d %%\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Input solution pH") ||
				!strcmp(pointName, "Extracted product pH")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T("Measurement: %.1f atm\n"), measurement);
				sprintf(stringMeasurements, "%.1f atm\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
		}
	}
	_tprintf(_T("\n"));
	strcpy(dataToWrite, "\n");
	writeToFile(dataToWrite, file);

	wcscpy(CommandBuf, _T("Ready")); //sends the command "Ready" automatically to the server. It will break when "Break" is manually send.
	SetEvent(hSend);

	return "";
}

	
