// **********************************************************************
//
// Copyright (c) 2001
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#include <Ice/UdpTransceiver.h>
#include <Ice/Instance.h>
#include <Ice/TraceLevels.h>
#include <Ice/Logger.h>
#include <Ice/Buffer.h>
#include <Ice/Network.h>
#include <Ice/Exception.h>

using namespace std;
using namespace Ice;
using namespace IceInternal;

SOCKET
IceInternal::UdpTransceiver::fd()
{
    return _fd;
}

void
IceInternal::UdpTransceiver::close()
{
    if (_traceLevels->network >= 1)
    {
	ostringstream s;
	s << "closing udp connection\n" << toString();
	_logger->trace(_traceLevels->networkCat, s.str());
    }

    SOCKET fd = _fd;
    _fd = INVALID_SOCKET;
    closeSocket(fd);
}

void
IceInternal::UdpTransceiver::shutdown()
{
}

void
IceInternal::UdpTransceiver::write(Buffer& buf, int)
{
    assert(buf.i == buf.b.begin());
#ifndef NDEBUG
    const int packetSize = 64 * 1024; // TODO: configurable
    assert(packetSize >= static_cast<int>(buf.b.size())); // TODO: exception
#endif

repeat:
    int ret = ::send(_fd, buf.b.begin(), buf.b.size(), 0);
    
    if (ret == SOCKET_ERROR)
    {
	if (interrupted())
	{
	    goto repeat;
	}

	SocketException ex(__FILE__, __LINE__);
	ex.error = getSocketErrno();
	throw ex;
    }

    if (_traceLevels->network >= 3)
    {
	ostringstream s;
	s << "sent " << ret << " bytes via " << _protocolName << "\n" << toString();
	_logger->trace(_traceLevels->networkCat, s.str());
    }
    
    assert(ret == static_cast<int>(buf.b.size()));
    buf.i = buf.b.end();
}

void
IceInternal::UdpTransceiver::read(Buffer& buf, int)
{
    assert(buf.i == buf.b.begin());
    const int packetSize = 64 * 1024; // TODO: configurable
    assert(packetSize >= static_cast<int>(buf.b.size())); // TODO: exception
    buf.b.resize(packetSize);
    buf.i = buf.b.begin();

repeat:
    int ret;
    if (_connect)
    {
	//
	// If we must connect, then we connect to the first peer that
	// sends us a packet.
	//
	struct sockaddr_in peerAddr;
	memset(&peerAddr, 0, sizeof(struct sockaddr_in));
	socklen_t len = sizeof(peerAddr);
	ret = recvfrom(_fd, buf.b.begin(), packetSize, 0, reinterpret_cast<struct sockaddr*>(&peerAddr), &len);
	if (ret != SOCKET_ERROR)
	{
	    doConnect(_fd, peerAddr, -1);
	    _connect = false; // We're connected now
	}
    }
    else
    {
	ret = ::recv(_fd, buf.b.begin(), packetSize, 0);
    }
    
    if (ret == SOCKET_ERROR)
    {
	if (interrupted())
	{
	    goto repeat;
	}
	
	SocketException ex(__FILE__, __LINE__);
	ex.error = getSocketErrno();
	throw ex;
    }
    
    if (_traceLevels->network >= 3)
    {
	ostringstream s;
	s << "received " << ret << " bytes via " << _protocolName << "\n" << toString();
	_logger->trace(_traceLevels->networkCat, s.str());
    }

    buf.b.resize(ret);
    buf.i = buf.b.end();
}

string
IceInternal::UdpTransceiver::toString() const
{
    return fdToString(_fd);
}

bool
IceInternal::UdpTransceiver::equivalent(const string& host, int port) const
{
    assert(_incoming); // This equivalence test is only valid for incoming connections.

    struct sockaddr_in addr;
    getAddress(host.c_str(), port, addr);
    if (addr.sin_addr.s_addr == htonl(INADDR_LOOPBACK))
    {
	return port == ntohs(_addr.sin_port);
    }

    struct sockaddr_in localAddr;
    getLocalAddress(ntohs(_addr.sin_port), localAddr);
    return memcmp(&addr, &localAddr, sizeof(struct sockaddr_in)) == 0;    
}

int
IceInternal::UdpTransceiver::effectivePort()
{
    return ntohs(_addr.sin_port);
}

void
IceInternal::UdpTransceiver::setProtocolName(const string& protocolName)
{
    _protocolName = protocolName;
}

IceInternal::UdpTransceiver::UdpTransceiver(const InstancePtr& instance,
                                            const string& host,
                                            int port,
                                            const string& protocolName) :
    _instance(instance),
    _traceLevels(instance->traceLevels()),
    _logger(instance->logger()),
    _incoming(false),
    _connect(true),
    _protocolName(protocolName)
{
    try
    {
	getAddress(host.c_str(), port, _addr);

	_fd = createSocket(true);
	doConnect(_fd, _addr, -1);
	_connect = false; // We're connected now
	
	if (_traceLevels->network >= 1)
	{
	    ostringstream s;
	    s << "starting to send " << _protocolName << " packets\n" << toString();
	    _logger->trace(_traceLevels->networkCat, s.str());
	}
    }
    catch(...)
    {
	_fd = INVALID_SOCKET;
	throw;
    }
}

IceInternal::UdpTransceiver::UdpTransceiver(const InstancePtr& instance, int port, bool connect,
					    const string& protocolName) :
    _instance(instance),
    _traceLevels(instance->traceLevels()),
    _logger(instance->logger()),
    _incoming(true),
    _connect(connect),
    _protocolName(protocolName)
{
    try
    {
	memset(&_addr, 0, sizeof(_addr));
	_addr.sin_family = AF_INET;
	_addr.sin_port = htons(port);
	_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	_fd = createSocket(true);
	doBind(_fd, _addr);
	    
	if (_traceLevels->network >= 1)
	{
	    ostringstream s;
	    s << "starting to receive " << _protocolName << " packets\n" << toString();
	    _logger->trace(_traceLevels->networkCat, s.str());
	}
    }
    catch(...)
    {
	_fd = INVALID_SOCKET;
	throw;
    }
}

IceInternal::UdpTransceiver::~UdpTransceiver()
{
    assert(_fd == INVALID_SOCKET);
}
