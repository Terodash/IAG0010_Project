#include "process.h"
#include "time.h"
#include "stdafx.h"
#include "Winsock2.h" // necessary for sockets, Windows.h is not needed.
#include "mswsock.h"

using namespace std;

class Event{
private : 
	HANDLE ev;
public : 
	Event() {};
	void set() { SetEvent(ev); }
	void reset() { ResetEvent(ev); }
	void create() { ev = CreateEvent(NULL, TRUE, FALSE, NULL); }
	void copy(Event cEvent) { ev = cEvent.ev; }
	void close() { CloseHandle(ev); }
	void beginThread() {}
	void getStd(){ ev = GetStdHandle(STD_INPUT_HANDLE); }
	HANDLE value() { return ev; }
};

class Socket {
private :
	HANDLE sock;
public :
	void set(){}
};

class WSAEvent : public Event {
public :
	void set() { WSASetEvent(ev); }
	void reset() { WSAResetEvent(ev); }
};

//#pragma once
//#pragma warning(disable : 4290)
//#pragma warning(disable : 4996)