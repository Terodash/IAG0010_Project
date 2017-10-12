// IAG0010PlantLogger.cpp : définit le point d'entrée pour l'application console.
//

#include "stdafx.h"
#include "Winsock2.h" // necessary for sockets, Windows.h is not needed.
#include "mswsock.h"
#include "IAG0010PlantLogger.h"
#include "process.h" 

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

// Global variables
TCHAR CommandBuf[81];
HANDLE hCommandGot; // event "the user has typed a command"
HANDLE hStopCommandGot; // event "the main thread has recognised that it was the stop command"
HANDLE hCommandProcessed; // event "the main thread has finished the processing of command"
HANDLE hReadKeyboard; // keyboard reading thread handle
HANDLE hStdIn; // standard input stream handle
WSADATA wsadata;
DWORD Error;
SOCKET hClientSocket = INVALID_SOCKET;
sockaddr_in ClientSocketInfo;
HANDLE hReceiveNet; //TCP/IP info reading thread
HANDLE hSendNet; //TCP/IP info sending thread
BOOL SocketError;
volatile BOOL threadStop = FALSE;
HANDLE hMutex;
HANDLE ThreadHandle;
DWORD ThreadId;
WSAEVENT AcceptEvent;

// Prototypes
unsigned int __stdcall ReadKeyboard(void* pArguments);
unsigned int __stdcall ReceiveNet(void* pArguments);
unsigned int __stdcall SendNet(void* pArguments);

//****************************************************************************************************************
//                                 MAIN THREAD
//****************************************************************************************************************
int _tmain(int argc, _TCHAR* argv[])
{
	hMutex = CreateMutex(NULL, FALSE, NULL);

	// Initializations for multithreading
	if (!(hCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hStopCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hCommandProcessed = CreateEvent(NULL, TRUE, TRUE, NULL)))
	{
		_tprintf(_T("CreateEvent() failed, error %d\n"), GetLastError());
		return 1;
	}

	// Prepare keyboard, start the thread
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

	// Initialization Socket
	if (Error = WSAStartup(MAKEWORD(2, 0), &wsadata)) {
		_tprintf(_T("WSAStartup failed, error %d\n"), Error);
		SocketError = TRUE;
	}

	else if ((hClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
		_tprintf(_T("socket() failed, error %d\n"), WSAGetLastError());
		SocketError = TRUE;
	}

	// Connecting client to the server
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

	// Start net thread
	if (!SocketError) {
		if (!(hReceiveNet = (HANDLE)_beginthreadex(NULL, 0, &ReceiveNet, NULL, 0, NULL))) {
			_tprintf(_T("Unable to create socket receiving thread\n"));
			goto out;
		}

		if (!(hSendNet = (HANDLE)_beginthreadex(NULL, 0, &SendNet, NULL, 0, NULL))) {
			_tprintf(_T("Unable to create socket sending thread\n"));
			goto out;
		}
	}

	while (TRUE) {
		if (WaitForSingleObject(hCommandGot, INFINITE) != WAIT_OBJECT_0) {
			_tprintf(_T("WaitForSingleObject() failed, error %d\n"), GetLastError());
			goto out;
		}
		ResetEvent(hCommandGot);
		if (!_tcsicmp(CommandBuf, _T("exit"))) {
			SetEvent(hStopCommandGot);
			break;
		}

		else if (!_tcsicmp(CommandBuf, _T("Start"))) {
			SetEvent(hCommandGot);
			_tprintf(_T("You entered the command Start\n"));
			break;
		}

		else if (!_tcsicmp(CommandBuf, _T("Stop"))) {
			SetEvent(hCommandGot);
			_tprintf(_T("You entered the command Stop\n"));
			break;
		}

		else if (!_tcsicmp(CommandBuf, _T("Break"))) {
			SetEvent(hCommandGot);
			_tprintf(_T("You entered the command Break\n"));
			break;
		}

		else if (!_tcsicmp(CommandBuf, _T("Ready"))) {
			SetEvent(hCommandGot);
			_tprintf(_T("You entered the command Ready\n"));
			break;
		}

		/*else if (!_tcsicmp(CommandBuf, _T("coursework"))) {
			SetEvent(hCommandGot);
			nSentBytes = 10;
			int Result1 = WSASend(hClientSocket, &DataBuf, 1, &nSentBytes, SentFlags, &Overlapped, NULL);
				_tprintf(_T("You entered the %c\n"), ArrayInBuf[0]);
		
			if (Result1 == SOCKET_ERROR) {

				if (Error = WSAGetLastError() != WSA_IO_PENDING) {// Unable to continue
					_tprintf(_T("WSASend() failed, error %d\n"), Error);
					goto out;
				}

				DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents, FALSE, WSA_INFINITE, FALSE);
				switch (WaitResult) {
				case WAIT_OBJECT_0:
					goto out;
				case WAIT_OBJECT_0 + 1:
					WSAResetEvent(NetEvents[1]);

					if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nSentBytes, FALSE, &SentFlags)) {
						// Here should follow the processing of received data
						_tprintf(_T("C'est ici qu'il faut gérer"));
						break;
					}
					else {// Fatal problems
						_tprintf(_T("WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
						goto out;
					}
				default: // Fatal problems
					_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
					goto out;
				}
			}
		}

		*/
		else {
			_tprintf(_T("Command \"%s\" not recognized\n"), CommandBuf);
			SetEvent(hCommandProcessed);
		}
	}

	// Shut down
out:
	if (hReadKeyboard) {
		WaitForSingleObject(hReadKeyboard, INFINITE);
		CloseHandle(hReadKeyboard);
	}

	if (hReceiveNet) {
		WaitForSingleObject(hReceiveNet, INFINITE);
		CloseHandle(hReceiveNet);
	}

	if (hSendNet) {
		WaitForSingleObject(hSendNet, INFINITE);
		CloseHandle(hSendNet);
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

	while (!threadStop)
		Sleep(0);
	CloseHandle(hMutex);

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
			WaitForSingleObject(hMutex, INFINITE);
			threadStop = TRUE;
			_tprintf(_T("Insert command\n"));

			if (!ReadConsole(hStdIn, CommandBuf, 80, &nReadChars, NULL)) {
				_tprintf(_T("ReadConsole() failed, error %d\n"), GetLastError());
				return 1;
			}
			CommandBuf[nReadChars - 2] = 0; // to get rid of \r\n
			ResetEvent(hCommandProcessed); //hCommandProcessed to non-signaled
			SetEvent(hCommandGot); // hCommandGot event to signaled


			threadStop = FALSE;
			ReleaseMutex(hMutex);
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
	// Preparations
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

	// Receiving loop
	while (TRUE) {
		Result = WSARecv(hClientSocket, &DataBuf, 1, &nReceivedBytes, &ReceiveFlags, &Overlapped, NULL);
		if (Result == SOCKET_ERROR) {

			if (Error = WSAGetLastError() != WSA_IO_PENDING) {// Unable to continue
				_tprintf(_T("WSARecv() failed, error %d\n"), Error);
				goto out;
			}


			WaitForSingleObject(hMutex, INFINITE);
			threadStop = TRUE;
			DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents, FALSE, WSA_INFINITE, FALSE);
			
			switch (WaitResult) {
			case WAIT_OBJECT_0:
				goto out;
			case WAIT_OBJECT_0 + 1:
				WSAResetEvent(NetEvents[1]);

				if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nReceivedBytes, FALSE, &ReceiveFlags)) {
					// Here should follow the processing of received data
					for (int i = 4; i <= 18; i = i + 2) {
						printf("%c", ArrayInBuf[i]);
					}
					printf("\n");
					break;
				}
				else {// Fatal problems
					_tprintf(_T("WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
					goto out;
				}
			default: // Fatal problems
				_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
				goto out;
			}

			threadStop = FALSE;
			ReleaseMutex(hMutex);
		}


		else {
			if (!nReceivedBytes) {
				_tprintf(_T("Server has closed the connection\n"));
				goto out;
			}
			else {
				_tprintf(_T("%d bytes received\n"), nReceivedBytes);
				// Here should follow the processing of received data 
			}
		}
	}
out:
	WSACloseEvent(NetEvents[1]);
	return 0;
}

//********************************************************************************************************************
//                          TCP/IP INFO SENDING THREAD
//********************************************************************************************************************
unsigned int __stdcall SendNet(void* pArguments) {

	_tprintf(_T("On est bien entré"));
	WSABUF DataBuf; // Buffer for sent data is a structure
	char ArrayInBuf[2048];
	DataBuf.buf = &ArrayInBuf[0];
	DataBuf.len = 2048;
	DWORD nSentBytes = 0; // Pointer to the number, in bytes, of data received by this call
	DWORD SentFlags = 0; // Pointer to flags used to modify the behaviour of the WSARecv function call
	HANDLE NetEvents[2];
	NetEvents[0] = hStopCommandGot;
	WSAOVERLAPPED Overlapped; 
	memset(&Overlapped, 0, sizeof(Overlapped));
	Overlapped.hEvent = NetEvents[1] = WSACreateEvent();
	DWORD Result, Error;
	_tprintf(_T("dfnklojnjfvo^ni"));
	
	while (TRUE) {
		Result = WSASend(hClientSocket, &DataBuf, 1, &nSentBytes, SentFlags, &Overlapped, NULL);

		if (Result == SOCKET_ERROR) {

			if (Error = WSAGetLastError() != WSA_IO_PENDING) {// Unable to continue
				_tprintf(_T("WSASend() failed, error %d\n"), Error);
				goto out;
			}

			WaitForSingleObject(hMutex, INFINITE);
			threadStop = TRUE;

			DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents, FALSE, WSA_INFINITE, FALSE);
			switch (WaitResult) {
			case WAIT_OBJECT_0:
				goto out;
			case WAIT_OBJECT_0 + 1:
				WSAResetEvent(NetEvents[1]);

				if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nSentBytes, FALSE, &SentFlags)) {
					WaitForSingleObject(hMutex, INFINITE);
					threadStop = TRUE;
					
					// Here should follow the processing of received data
					_tprintf(_T("C'est en train de s'envoyer\n"));

					threadStop = FALSE;
					ReleaseMutex(hMutex);
					break;
				}
				else {// Fatal problems
					_tprintf(_T("WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
					goto out;
				}
			default: // Fatal problems
				_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
				goto out;
			}

		}
	}
out:
	_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
	WSACloseEvent(NetEvents[1]);
	return 0;
}
