#include "buffer.hh"
#include <errno.h>


/**
 * Buffer:
 *
 *    +-----------+-------------+-----------------+
 *    |    sent   |  wait send  |    wait write   |
 *    +-----------+-------------+-----------------+  
 *    ^           ^             ^ 
 *  _start    _wait_send    _wait_write 
 *
 */

Buffer::Buffer(int size) :
	_size(size)
{
	_start = new char[size];
	_wait_send = _wait_write = _start;
}

Buffer::~Buffer()
{
	delete[] _start;
}

int 
Buffer::read_nonblock(int fd, int nbytes)
{
	int n;
	int left = &(_start[_size]) - _wait_write;
	nbytes = nbytes < left ? nbytes : left;

	if ( (n = read(fd, _wait_write, nbytes)) > 0)
		_wait_write += n;
	
	return n;
}

int 
Buffer::read_nonblock(int fd)
{
	int n;
	int left = &(_start[_size]) - _wait_write;

	if ( (n = read(fd, _wait_write, left)) > 0)
		_wait_write += n;

	return n;
}

int 
Buffer::write_nonblock(int fd, int nbytes)
{
	int n;
	int left = _wait_write - _wait_send;
	nbytes = nbytes < left ? nbytes : left;

	if ( (n = write(fd, _wait_send, nbytes)) > 0) {
		_wait_send += n;
		if (_wait_send == _wait_write)
			_wait_send = _wait_write = _start;
	}

	return  n;
}

int
Buffer::write_nonblock(int fd)
{
	int n;
	int left = _wait_write - _wait_send;

	if ( (n = write(fd, _wait_send, left)) > 0) {
		_wait_send += n;
	}
	return n;
}
