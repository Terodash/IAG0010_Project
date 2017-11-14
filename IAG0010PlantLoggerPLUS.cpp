
#include "IAG0010PlantLoggerPLUS.h"

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

//Events
Event hCommandGot;			// "User typed a command"
Event hStopCommandGot; 		// "Stop command recognized by the Main thread"
Event hCommandProcessed; 	// "Main thread finished processing a command"
Event hConnectCommandGot; 	// "Connexion requested"
Event hSendPassword; 		// "Emulator requesting password"
Event hSend; 				// "Data has been sent"

Event hReadKeyboard; 		// Keyboard reading thread 
Event hStdIn; 				// Standard input stream 
Event hReceiveNet; 			//TCP/IP info receiving thread
Event hSendNet; 			//TCP/IP info sending thread

							//Variables for receiving thread
WSAOVERLAPPED receiveOverlapped;
Event WSAReceiveCompletedEvents[2];
Event ReceivedEvents[2];

TCHAR CommandBuf[81];		//Buffer for the command written by the user
BOOL startOK = FALSE;
BOOL connected = FALSE;
BOOL sendConnectionNotAccepted = TRUE;

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
	CloseHandle(file);
	closesocket(hClientSocket);
	hReadKeyboard.close;
	hReceiveNet.close;
	hStopCommandGot.close;
	hCommandGot.close;
	hCommandProcessed.close;
	//
	// Initializations for multithreading
	//
	if (!(hCommandGot.create) ||
		!(hStopCommandGot.create) ||
		!(hCommandProcessed.create) ||
		!(hSend.create) ||
		!(hConnectCommandGot.create))
	{
		_tprintf(_T("CreateEvent() failed, error %d\n"), GetLastError());
		return 1;
	}

	_tprintf(_T("Welcome in the Logger system\nPlease enter your password\n"));

	WSAReceiveCompletedEvents[0].copy(hStopCommandGot);
	ReceivedEvents[0].copy(hStopCommandGot);
	memset(&receiveOverlapped, 0, sizeof receiveOverlapped);
	receiveOverlapped.hEvent = WSAReceiveCompletedEvents[1] = WSACreateEvent();
	ReceivedEvents[1].create;

	if (!(WSAReceiveCompletedEvents[1].value) || !(ReceivedEvents[1].value)) {//privacy problem
		_tprintf(_T("CreateEvent() failed for network events, error %d\n"), GetLastError());
		goto out_main;
	}

	//
	// Prepare keyboard and starts ReadKeyboard thread
	//
	hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdIn.value == INVALID_HANDLE_VALUE) {
		_tprintf(_T("GetStdHandle() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!SetConsoleMode(hStdIn.value, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) {
		_tprintf(_T("SetConsoleMode() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!(hReadKeyboard.value = (HANDLE)_beginthreadex(NULL, 0, &ReadKeyboard, NULL, 0, NULL))) {
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

		if (WaitForSingleObject(hCommandGot.value, INFINITE) != WAIT_OBJECT_0) {
			_tprintf(_T("WaitForSingleObject() failed, error %d\n"), GetLastError());
			goto out_main;
		}
		hCommandGot.reset;

		if (!connected) {
			if (!_tcsicmp(CommandBuf, _T("connect"))) { //used for test
				hCommandProcessed.set; //keyboard thread can continue working
				sendConnectionNotAccepted = TRUE;
				connected = FALSE;
				hSend.set;
				hConnectCommandGot.set;
				argv[argc] = NULL;
				return _tmain(argc, argv);
			}
			else if (!_tcsicmp(CommandBuf, _T("exit"))) { //user typed "exit" command
				_tprintf(_T("Terminating...\n"));
				hStopCommandGot.set;
				break;
			}
			else {
				hCommandProcessed.set; //keyboard thread can continue working
				hReadKeyboard.set;
				hSend.set;
			}
		}
		else {
			if (!_tcsicmp(CommandBuf, _T("exit"))) { //user typed "exit" command
				_tprintf(_T("Terminating...\n"));
				hStopCommandGot.set;
				break;
			}

			else if (!_tcsicmp(CommandBuf, _T("start"))) { //used for test
				_tprintf(_T("Command start accepted.\n"));
				hCommandProcessed.set; //keyboard thread can continue working
				CommandBuf[0] = 'S';
				startOK = TRUE;
				sendConnectionNotAccepted = TRUE;
				hSend.set;
			}

			else if (!_tcsicmp(CommandBuf, _T("break"))) { //used for test
				_tprintf(_T("Command break accepted.\n"));
				CommandBuf[0] = 'B';
				hCommandProcessed.set; //keyboard thread can continue working
				hSend.set;
			}

			else if (!_tcsicmp(CommandBuf, _T("stop"))) { //used for test
				_tprintf(_T("Command stop accepted.\n"));
				hCommandProcessed.set; //keyboard thread can continue working
				CommandBuf[0] = 'S';
				hSend.set;
				sendConnectionNotAccepted = FALSE;
				connected = FALSE;
				startOK = FALSE;
			}

			else {
				_tprintf(_T("The command is not recognized.\n"));
				hCommandProcessed.set;
			}
		}
	}

	// Shut down
out_main:
	if (hReadKeyboard.value) {
		WaitForSingleObject( hReadKeyboard.value, INFINITE);
		hReadKeyboard.set;
	}

	if (hReceiveNet.value) {
		WaitForSingleObject(hReceiveNet, INFINITE);
		hReceiveNet);
	}
	if (hClientSocket != INVALID_SOCKET) {
		if (shutdown(hClientSocket, SD_RECEIVE) == SOCKET_ERROR) {
			if ((Error = WSAGetLastError()) != WSAENOTCONN)
				_tprintf(_T("shutdown() failed, error %d\n"), WSAGetLastError());
		}
		closesocket(hClientSocket);
	}
	WSACleanup();
	hStopCommandGot.close;
	hCommandGot.close;
	hCommandProcessed.close;
	return 0;
}

//**************************************************************************************************************
//                          KEYBOARD READING THREAD
//**************************************************************************************************************
unsigned int __stdcall ReadKeyboard(void* pArguments) {
	DWORD nReadChars;
	Event KeyboardEvents[2];
	KeyboardEvents[1].copy(hCommandProcessed);
	KeyboardEvents[0].copy(hStopCommandGot);
	DWORD WaitResult;

	// Reading loop
	while (TRUE) {
		WaitResult = WaitForMultipleObjects(2, KeyboardEvents.value, FALSE, INFINITE);
		if (WaitResult == WAIT_OBJECT_0)
			return 0; // Stop command got
		else if (WaitResult == WAIT_OBJECT_0 + 1) { // command processed
													//_tprintf(_T("Insert command\n"));
			if (!ReadConsole(hStdIn.value, CommandBuf, 80, &nReadChars, NULL)) { //to be improved
				_tprintf(_T("ReadConsole() failed, error %d\n"), GetLastError());
				return 1;
			}
			CommandBuf[nReadChars - 2] = 0; // to get rid of \r\n
			hCommandProcessed.reset; //hCommandProcessed to non-signaled
			hCommandGot.set; // hCommandGot event to signaled
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
	Event NetEvents[2];
	NetEvents[0].copy(hStopCommandGot);
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
	file = CreateFile(output_file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

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

			DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents.value, FALSE, WSA_INFINITE, FALSE); //wait for received data

			switch (WaitResult) {
			case WAIT_OBJECT_0:		//hStopCommandGot signaled, user wants to exit
				goto out_receive;

			case WAIT_OBJECT_0 + 1: //Overlapped.hEvent signaled, received operation is over
				WSAResetEvent(NetEvents[1].value); //To be ready for the next data package

				if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nReceivedBytes, FALSE, &ReceiveFlags)) {
					printf("Message from emulator: ");
					int i = 0;

					if (startOK == TRUE) {
						displayAndWrite(&ArrayInBuf[0]);
					}

					else if (sendConnectionNotAccepted) {
						for (i = 4; i <= (int) nReceivedBytes - 2; i = i + 2) {
							printf("%c", ArrayInBuf[i]);
						}
						printf("\n");

						DataPointer = (wchar_t*)(DataBuf.buf + 4);

						if (!wcscmp(DataPointer, identifier)) //We have received an identification request from the Emulator
							hSendPassword.set;

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
	WSABUF sendDataBuf; // Buffer for sent data
	char message[50] = "";
	sendDataBuf.buf = &message[0];
	sendDataBuf.len = 2048;

	DWORD nSendBytes = 0; // Pointer to the number, in bytes, of data sent by this call
	DWORD SendFlags = 0; // Pointer to flags used to modify the behaviour of the WSASend function call
	DWORD waitResult = 0;

	//Variables for sending thread
	WSAEvent SentEvents[3];
	SentEvents[0].copy(hStopCommandGot);
	WSAOVERLAPPED sendOverlapped;
	memset(&sendOverlapped, 0, sizeof sendOverlapped);
	sendOverlapped.hEvent = SentEvents[1] = WSACreateEvent();
	SentEvents[2].copy(hSend);
	DWORD Error, Result;

	while (TRUE) {
		waitResult = WSAWaitForMultipleEvents(3, SentEvents, FALSE, WSA_INFINITE, FALSE); //Waiting for data
		switch (waitResult) {

		case WAIT_OBJECT_0:
			//Stop has been signaled by user
			goto out_send;

		case WAIT_OBJECT_0 + 1:
			SentEvents[1].reset;
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
			sendDataBuf.len = wcslen(CommandBuf) * 2 + 6;
			memcpy(sendDataBuf.buf, &sendDataBuf.len, 4);
			memcpy(sendDataBuf.buf + 4, (char *)CommandBuf, sendDataBuf.len);
			nSendBytes = sendDataBuf.len;

			Result = WSASend(hClientSocket, &sendDataBuf, 1, &nSendBytes, SendFlags, &sendOverlapped, NULL);
			break;

		default: // Fatal problems
			_tprintf(_T("Sending thread WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
			goto out_send;
		}
		SentEvents[1].reset;
		SentEvents[2].reset;
	}
out_send:
	_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
	SentEvents[1].close;
	return 0;
}

int writeToFile(char *dataToWrite, HANDLE file) {
	int length = strlen(dataToWrite);
	DWORD nBytesWritten;

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
	sprintf(currentTime, "Measurements at %d-%02d-%02d %02d:%02d:%02d \n",
		t->tm_year + 1900, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	printf(currentTime);
	strcpy(dataToWrite, currentTime);	//we copy the strings about time to the data needed to be written in the file
	strcat(dataToWrite, "\n");			//and concatenate a \n for a proper presentation
	writeToFile(dataToWrite, file); //data is written to our file

	for (int i = 0; i < channels_number; i++) { //we repeat the procedure for each channel
		int pointsNumber; //number of measurement points in the active channel

		memcpy(&pointsNumber, data + position, sizeof(int));
		position = position + sizeof(int);
		_tprintf(_T("\nNumber of measurement points for the active channel: %d\n"), pointsNumber);

		char channelName[2048]; //name of the active channel
		memccpy(channelName, data + position, '\0', 20);
		printf("Channel name: %s\n", channelName);
		strcpy(dataToWrite, channelName);
		strcat(dataToWrite, ":\n");
		writeToFile(dataToWrite, file);
		position = position + strlen(channelName) + 1;

		for (int j = 0; j < pointsNumber; j++) { //we repeat the procedure for each point
			char pointName[2048]; //name of the active point
			memccpy(pointName, data + position, '\0', 35);
			printf("  %s", pointName);
			strcpy(dataToWrite, pointName);
			strcat(dataToWrite, ": ");
			writeToFile(dataToWrite, file);
			position = position + strlen(pointName) + 1;

			char stringMeasurements[2048]; //data that contains numerical values

			if (!strcmp(pointName, "Input solution flow") ||
				!strcmp(pointName, "Output solution flow") ||
				!strcmp(pointName, "Input gas flow") ||
				!strcmp(pointName, "Input steam flow")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T(" : %.7f m%c/s\n"), measurement, 252);
				sprintf(stringMeasurements, "%.7f m³/s\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Input solution temperature") ||
				!strcmp(pointName, "Input steam temperature")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T(" : %.6f %cC\n"), measurement, 248);
				sprintf(stringMeasurements, "%.6f °C\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Input solution pressure") ||
				!strcmp(pointName, "Input gas pressure")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T(" : %.7f atm\n"), measurement);
				sprintf(stringMeasurements, "%.7f atm\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Level")) {
				int measurement;
				memcpy(&measurement, data + position, sizeof(int));
				position = position + sizeof(int);
				_tprintf(_T(" : %d %%\n"), measurement);
				sprintf(stringMeasurements, "%d %%\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Output solution conductivity")) {
				double measurement;
				memcpy(&measurement, data + position, sizeof(double));
				position = position + sizeof(double);
				_tprintf(_T(" : %.2f S/m\n"), measurement);
				sprintf(stringMeasurements, "%.2f S/m\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
			else if (!strcmp(pointName, "Output solution concentration")) {
				int measurement;
				memcpy(&measurement, data + position, sizeof(int));
				position = position + sizeof(int);
				_tprintf(_T(" : %d %%\n"), measurement);
				sprintf(stringMeasurements, "%d %%\n", measurement);
				strcpy(dataToWrite, stringMeasurements);
				writeToFile(dataToWrite, file);
			}
		}
	}
	_tprintf(_T("\n"));
	strcpy(dataToWrite, "\n");
	writeToFile(dataToWrite, file);

	wcscpy(CommandBuf, _T("Ready")); //sends the command "Ready" automatically to the server. It will break when "Break" is manually send.
	hSend.set;

	return "";
}