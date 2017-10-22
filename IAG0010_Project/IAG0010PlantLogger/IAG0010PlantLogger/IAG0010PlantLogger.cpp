#include "stdafx.h"
#include "Winsock2.h" // necessary for sockets, Windows.h is not needed.
#include "mswsock.h"
#include "IAG0010PlantLogger.h"
#include "process.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

HANDLE file; //File where we will write the received data

			 //Socket variables
WSADATA wsadata; //Windows sockets initialization
DWORD Error;
SOCKET hClientSocket = INVALID_SOCKET;
sockaddr_in ClientSocketInfo;
BOOL SocketError;

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
		
		if (!_tcsicmp(CommandBuf, _T("exit"))) { //user typed "exit" command
			_tprintf(_T("Terminating...\n"));
			SetEvent(hStopCommandGot);
			break;

		} 
		
		else if (!_tcsicmp(CommandBuf, _T("coursework"))) { //used for test
			_tprintf(_T("Command Connect accepted, nothing to do yet.\n"));
			SetEvent(hConnectCommandGot); //user requested to connect
			SetEvent(hCommandProcessed); //keyboard thread can continue working
			SetEvent(hSend);
		}
		
		else if (!_tcsicmp(CommandBuf, _T("Start"))) { //used for test
			_tprintf(_T("Command Start accepted.\n"));
			SetEvent(hCommandProcessed); //keyboard thread can continue working
			SetEvent(hSend);
			startOK = TRUE;
		}

		else {
			_tprintf(_T("Command \"%s\" not recognized\n"), CommandBuf);
			SetEvent(hCommandProcessed); //keyboard thread can continue working
		}

		//CommandBuf[0] = 'X'; //test for "resetting" the CommandBuf


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
	wchar_t *DataPointer;
	int n = 0;

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

					_tprintf(_T("%d bytes received. "), nReceivedBytes);
					printf("Message received is: ");
					
					if (startOK == TRUE) {
						double measurement;
						//printf("Number of channels : %d\n", ArrayInBuf[4]);
						//printf("Number of of points in the channel 1 : %d\n", ArrayInBuf[8]);
						printf("\n\nMeasurement results:\n");

						/* Extractor */
						for (i = 12; i < 21; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(":\n");

						/* Input Solution Flow */
						for (i = 22; i < 41; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[42], sizeof(double));
						_tprintf(_T("%.7fm%c/s"), measurement, 252);
						printf("\n");

						/* Input Solution Temperature */
						for (i = 50; i<76; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);						
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[77], sizeof(double));
						_tprintf(_T("%2.4f%cC"), measurement, 248);
						printf("\n");						
						
						/* Input Solution Pressure */
						for (i = 85; i<108; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": "); 
						memcpy(&measurement, &ArrayInBuf[109], sizeof(double));
						_tprintf(_T("%1.5fatm"), measurement);
						printf("\n");

						/* Input Solution pH */
						for (i = 117; i<134; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[135], sizeof(double));
						_tprintf(_T("%1.5f"), measurement);
						printf("\n");

						/* Extracted Product concentration */
						for (i = 143; i<174; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[175], sizeof(int));
						_tprintf(_T("%d"), measurement);
						printf("%%\n");

						/* Extracted Product pH */
						for (i = 179; i<199; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[200], sizeof(double));
						_tprintf(_T("%1.5f"), measurement);
						printf("\n");

						/* Oxidizer */
						for (i = 212; i<220; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(":\n");

						/* Input Air Pressure */
						for (i = 221; i<239; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[240], sizeof(double));
						_tprintf(_T("%1.6fatm"), measurement);
						printf("\n");

						/* Output Liquid Flow*/
						for (i = 248; i<266; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": "); 
						memcpy(&measurement, &ArrayInBuf[267], sizeof(double));
						_tprintf(_T("%1.5fm%c/s"), measurement, 252);
						printf("\n");

						/* Separator */
						for (i = 279; i<288; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(":\n");

						/* Input Liquid Flow */
						for (i = 289; i<306; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": "); 
						memcpy(&measurement, &ArrayInBuf[307], sizeof(double));
						_tprintf(_T("%1.6fm%c/s"), measurement, 252);
						printf("\n");

						/* Disulfid on output */
						for (i = 315; i<333; i++)
							_tprintf(_T("%c"), ArrayInBuf[i]);
						printf(": ");
						memcpy(&measurement, &ArrayInBuf[334], sizeof(int));
						_tprintf(_T("%d"), measurement);
						printf("%%\n");
					}
					
					else if (sendConnectionNotAccepted){
						for (i = 4; i <= nReceivedBytes - 2; i = i + 2) {
							printf("%c", ArrayInBuf[i]);
						}
						printf("\n");

						DataPointer = (wchar_t*)(DataBuf.buf + 4);

						if (!wcscmp(DataPointer, identifier)) //We have received an identification request from the Emulator
						{
							_tprintf(_T("Identification requested...\n"));
							SetEvent(hSendPassword);
						}
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
	sendDataBuf.buf = &message[0];                         //plus tard: mettre =&ArrayInBuf[0]
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
			/*wcscpy_s(data, _T("coursework"));
			dataLength = sizeof("coursework");
			_tprintf(_T("Sending : %s and size is: %d\n"), data, dataLength);
			memcpy(sendDataBuf.buf, &dataLength, 4);
			sendDataBuf.buf[0] = 26;
			memcpy(sendDataBuf.buf + 4, data, dataLength + 8);
			sendDataBuf.len = sizeof("coursework")*2 + 4;
			nSendBytes = (sizeof(sendDataBuf.buf)) - 1;
			Result = WSASend(hClientSocket, &sendDataBuf, 1, &nSendBytes, SendFlags, &sendOverlapped, NULL);
			*/
			
			strcpy(data, (char *)CommandBuf);
			_tprintf(_T("Sending : %s\n"), (char *)CommandBuf);
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
