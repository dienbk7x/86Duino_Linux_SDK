/*
  TwoWire.cpp - TWI/I2C library for Wiring & Arduino
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 
  Modified 2012 by Todd Krein (todd@krein.org) to implement repeated starts
  Modified 01 November 2013 by Android Lin
*/

#include <stdlib.h>
#include <string.h>
#include "twi.h"

#include "Wire.h"

#if defined (DMP_LINUX)
  #include <pthread.h>
  pthread_spinlock_t rxBuffLock;
  pthread_spinlock_t txBuffLock;
  pthread_spinlock_t multiWireLock;
#endif

// Initialize Class Variables //////////////////////////////////////////////////

uint8_t TwoWire::rxBuffer[BUFFER_LENGTH];
uint8_t TwoWire::rxBufferIndex = 0;
uint8_t TwoWire::rxBufferLength = 0;

uint8_t TwoWire::txAddress = 0;
uint8_t TwoWire::txBuffer[BUFFER_LENGTH];
uint8_t TwoWire::txBufferIndex = 0;
uint8_t TwoWire::txBufferLength = 0;

uint8_t TwoWire::transmitting = 0;
void (*TwoWire::user_onRequest)(void) = NULL;
void (*TwoWire::user_onReceive)(int) = NULL;

// Constructors ////////////////////////////////////////////////////////////////

TwoWire::TwoWire()
{
#if defined (DMP_LINUX)
	pthread_spin_init(&rxBuffLock, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&txBuffLock, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&multiWireLock, PTHREAD_PROCESS_SHARED);
#endif
}

// Public Methods //////////////////////////////////////////////////////////////

void TwoWire::begin(void)
{
#if defined (DMP_LINUX)
  pthread_spin_lock(&txBuffLock);
  pthread_spin_lock(&rxBuffLock);
#endif

  rxBufferIndex = 0;
  rxBufferLength = 0;

  txBufferIndex = 0;
  txBufferLength = 0;
  
#if defined (DMP_LINUX)
  pthread_spin_unlock(&rxBuffLock);
  pthread_spin_unlock(&txBuffLock);
#endif
  twi_init();
}

void TwoWire::begin(uint8_t address)
{
  begin();
  twi_setAddress(address);
  twi_attachSlaveTxEvent(onRequestService);
  twi_attachSlaveRxEvent(onReceiveService);
}

void TwoWire::begin(uint32_t speed, uint8_t address)
{
#if defined (DMP_LINUX)
  pthread_spin_lock(&txBuffLock);
  pthread_spin_lock(&rxBuffLock);
#endif

  rxBufferIndex = 0;
  rxBufferLength = 0;

  txBufferIndex = 0;
  txBufferLength = 0;

#if defined (DMP_LINUX)
  pthread_spin_unlock(&rxBuffLock);
  pthread_spin_unlock(&txBuffLock);
#endif

  twi_init(speed);
  twi_setAddress(address);
  twi_attachSlaveTxEvent(onRequestService);
  twi_attachSlaveRxEvent(onReceiveService);
}

void TwoWire::begin(int address)
{
  begin((uint8_t)address);
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop)
{
  if(quantity == 0) return 0;
  
  // clamp to buffer length
  if(quantity > BUFFER_LENGTH){
    quantity = BUFFER_LENGTH;
  }
  // perform blocking read into buffer
  uint8_t read = twi_readFrom(address, rxBuffer, quantity, sendStop);
  
#if defined (DMP_LINUX)
  pthread_spin_lock(&rxBuffLock);
#endif
  // set rx buffer iterator vars
  rxBufferIndex = 0;
  rxBufferLength = read;
  
#if defined (DMP_LINUX)
  pthread_spin_unlock(&rxBuffLock);
#endif

  return read;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWire::requestFrom(int address, int quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWire::requestFrom(int address, int quantity, int sendStop)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)sendStop);
}

void TwoWire::beginTransmission(uint8_t address)
{
#if defined (DMP_LINUX)
  pthread_spin_lock(&txBuffLock);
#endif
  // indicate that we are transmitting
  transmitting = 1;
  // set address of targeted slave
  txAddress = address;
  // reset tx buffer iterator vars
  txBufferIndex = 0;
  txBufferLength = 0;

#if defined (DMP_LINUX)
  pthread_spin_unlock(&txBuffLock);
#endif
}

void TwoWire::beginTransmission(int address)
{
  beginTransmission((uint8_t)address);
}

//
//	Originally, 'endTransmission' was an f(void) function.
//	It has been modified to take one parameter indicating
//	whether or not a STOP should be performed on the bus.
//	Calling endTransmission(false) allows a sketch to 
//	perform a repeated start. 
//
//	WARNING: Nothing in the library keeps track of whether
//	the bus tenure has been properly ended with a STOP. It
//	is very possible to leave the bus in a hung state if
//	no call to endTransmission(true) is made. Some I2C
//	devices will behave oddly if they do not see a STOP.
//
uint8_t TwoWire::endTransmission(uint8_t sendStop)
{
  //if(txBufferLength == 0) return 0;
  // transmit buffer (blocking)
  int8_t ret = twi_writeTo(txAddress, txBuffer, txBufferLength, 1, sendStop);
  
#if defined (DMP_LINUX)
  pthread_spin_lock(&txBuffLock);
#endif

  // reset tx buffer iterator vars
  txBufferIndex = 0;
  txBufferLength = 0;
  // indicate that we are done transmitting
  transmitting = 0;
  
#if defined (DMP_LINUX)
  pthread_spin_unlock(&txBuffLock);
#endif
  return ret;
}

//	This provides backwards compatibility with the original
//	definition, and expected behaviour, of endTransmission
//
uint8_t TwoWire::endTransmission(void)
{
  return endTransmission(true);
}

// must be called in:
// slave tx event callback
// or after beginTransmission(address)
size_t TwoWire::write(uint8_t data)
{
  int master, writeLenght;

#if defined (DMP_LINUX)
  pthread_spin_lock(&txBuffLock);
#endif
  master = transmitting;
  writeLenght = txBufferLength;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&txBuffLock);
#endif

  if(master){
  // in master transmitter mode
    // don't bother if buffer is full
    if(writeLenght >= BUFFER_LENGTH){
      setWriteError();
      return 0;
    }
#if defined (DMP_LINUX)
    pthread_spin_lock(&txBuffLock);
#endif
    // put byte in tx buffer
    txBuffer[txBufferIndex] = data;
    ++txBufferIndex;
    // update amount in buffer   
    txBufferLength = txBufferIndex;
#if defined (DMP_LINUX)
    pthread_spin_unlock(&txBuffLock);
#endif
  }else{
  // in slave send mode
    // reply to master
    twi_transmit(&data, 1);
  }
  return 1;
}

// must be called in:
// slave tx event callback
// or after beginTransmission(address)
size_t TwoWire::write(const uint8_t *data, size_t quantity)
{
  int master;
  
#if defined (DMP_LINUX)
  pthread_spin_lock(&txBuffLock);
#endif
  master = transmitting;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&txBuffLock);
#endif 
  if(master){
  // in master transmitter mode
    for(size_t i = 0; i < quantity; ++i){
      write(data[i]);
    }
  }else{
  // in slave send mode
    // reply to master
    twi_transmit(data, quantity);
  }
  return quantity;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWire::available(void)
{
  int ret;

#if defined (DMP_LINUX)
  pthread_spin_lock(&rxBuffLock);
#endif
  ret = rxBufferLength - rxBufferIndex;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&rxBuffLock);
#endif
  return ret;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWire::read(void)
{
  int value = -1;

#if defined (DMP_LINUX)
  pthread_spin_lock(&rxBuffLock);
#endif
  // get each successive byte on each call
  if(rxBufferIndex < rxBufferLength){
    value = rxBuffer[rxBufferIndex];
    ++rxBufferIndex;
  }
#if defined (DMP_LINUX)
  pthread_spin_unlock(&rxBuffLock);
#endif
  return value;
}

// must be called in:
// slave rx event callback
// or after requestFrom(address, numBytes)
int TwoWire::peek(void)
{
  int value = -1;

#if defined (DMP_LINUX)
  pthread_spin_lock(&rxBuffLock);
#endif
  if(rxBufferIndex < rxBufferLength){
    value = rxBuffer[rxBufferIndex];
  }
#if defined (DMP_LINUX)
  pthread_spin_unlock(&rxBuffLock);
#endif
  return value;
}

void TwoWire::flush(void)
{
  // XXX: to be implemented.
}

// behind the scenes function that is called when data is received
void TwoWire::onReceiveService(uint8_t* inBytes, int numBytes)
{
#if defined (DMP_DOS_BC) || defined (DMP_DOS_DJGPP)
  // don't bother if user hasn't registered a callback
  if(user_onReceive == NULL){
    return;
  }
  // don't bother if rx buffer is in use by a master requestFrom() op
  // i know this drops data, but it allows for slight stupidity
  // meaning, they may not have read all the master requestFrom() data yet
  if(rxBufferIndex < rxBufferLength){
    return;
  }
  // copy twi rx buffer into local read buffer
  // this enables new reads to happen in parallel
  for(uint8_t i = 0; i < numBytes; ++i){
    rxBuffer[i] = inBytes[i];
  }
  // set rx iterator vars
  rxBufferIndex = 0;
  rxBufferLength = numBytes;
  // alert user program
  user_onReceive(numBytes);
#endif
}

// behind the scenes function that is called when data is requested
void TwoWire::onRequestService(void)
{
#if defined (DMP_DOS_BC) || defined (DMP_DOS_DJGPP)
  // don't bother if user hasn't registered a callback
  if(user_onRequest == NULL){
    return;
  }
  // reset tx buffer iterator vars
  // !!! this will kill any pending pre-master sendTo() activity
  txBufferIndex = 0;
  txBufferLength = 0;
  // alert user program
  user_onRequest();
#endif
}

// sets function called on slave write
void TwoWire::onReceive( void (*function)(int) )
{
#if defined (DMP_DOS_BC) || defined (DMP_DOS_DJGPP)
  user_onReceive = function;
#endif
}

// sets function called on slave read
void TwoWire::onRequest( void (*function)(void) )
{
#if defined (DMP_DOS_BC) || defined (DMP_DOS_DJGPP)
  user_onRequest = function;
#endif
}

#if defined (DMP_LINUX)
bool TwoWire::send(uint8_t addr, uint8_t* data, int datasize) {
	int i;
	bool err = false;
	if(data == NULL || datasize == 0) return false;

	pthread_spin_lock(&multiWireLock);
	
	Wire.beginTransmission(addr);
	for(i=0; i<datasize; i++) Wire.write(data[i]);
	if(Wire.endTransmission() != 0) {printf("%s error\n", __FUNCTION__); err = true;}
	
	pthread_spin_unlock(&multiWireLock);
	
	return (err == true) ? false : true;
}

bool TwoWire::receive(uint8_t addr, uint8_t* buf, uint8_t bufsize) {
	int i, read;
	bool err = false;
	if(buf == NULL || bufsize == 0) return 0;
	
	pthread_spin_lock(&multiWireLock);
	
	read = Wire.requestFrom(addr, bufsize);
	if(read < bufsize) {printf("The  bytes read by I2C are less than input buff size\n"); err = true;}
	for(i=0; i<read; i++) buf[i] = Wire.read();
	
	pthread_spin_unlock(&multiWireLock);
	
	return (err == true) ? false : true;
}

bool TwoWire::sensorRead(uint8_t addr, uint8_t cmd, uint8_t* buf, uint8_t bufsize) {
	return sensorReadEX(addr, &cmd, 1, buf, bufsize);
}
bool TwoWire::sensorReadEX(uint8_t addr, uint8_t* cmds, int cmdsize, uint8_t* buf, uint8_t bufsize) {
	int i, read;
	bool err = false;

	if(cmds == NULL || buf == NULL || cmdsize == 0 || bufsize == 0) return false;

	pthread_spin_lock(&multiWireLock);

	Wire.beginTransmission(addr);
	for(i=0; i<cmdsize; i++) Wire.write(cmds[i]);
	if(Wire.endTransmission(false) != 0) {printf("%s error\n", __FUNCTION__); err = true;} // send restart 
	read = Wire.requestFrom(addr, bufsize);
	if(read < bufsize) {printf("The  bytes read by I2C are less than input buff size\n"); err = true;}
	for(i=0; i<read; i++) buf[i] = Wire.read();

	pthread_spin_unlock(&multiWireLock);
	
	return (err == true) ? false : true;
}
#endif



#if defined (DMP_LINUX)
  pthread_spinlock_t sw_rxBuffLock;
  pthread_spinlock_t sw_txBuffLock;
  pthread_spinlock_t sw_multiWireLock;
#endif

// Initialize Class Variables //////////////////////////////////////////////////

uint8_t TwoWireLEGO::rxBuffer[BUFFER_LENGTH];
uint8_t TwoWireLEGO::rxBufferIndex = 0;
uint8_t TwoWireLEGO::rxBufferLength = 0;

uint8_t TwoWireLEGO::txAddress = 0;
uint8_t TwoWireLEGO::txBuffer[BUFFER_LENGTH];
uint8_t TwoWireLEGO::txBufferIndex = 0;
uint8_t TwoWireLEGO::txBufferLength = 0;

uint8_t TwoWireLEGO::transmitting = 0;

// Constructors ////////////////////////////////////////////////////////////////

TwoWireLEGO::TwoWireLEGO()
{
#if defined (DMP_LINUX)
	pthread_spin_init(&sw_rxBuffLock, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&sw_txBuffLock, PTHREAD_PROCESS_SHARED);
#endif
}

// Public Methods //////////////////////////////////////////////////////////////
void TwoWireLEGO::begin(unsigned long nHz)
{
#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_txBuffLock);
  pthread_spin_lock(&sw_rxBuffLock);
#endif
  rxBufferIndex  = 0;
  rxBufferLength = 0;

  txBufferIndex  = 0;
  txBufferLength = 0;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_rxBuffLock);
  pthread_spin_unlock(&sw_txBuffLock);
#endif

  if(nHz == 0) nHz = 10000L; // default

  unsigned long _delay = 1000000L/nHz; // us
  if(_delay < 10L) _delay = 10L; // up to 50KHz

  twisw_init(0, I2CSW_LEGO, _delay/2L, I2CSW_LEGO, _delay/2L);
}

uint8_t TwoWireLEGO::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop)
{
  if(quantity == 0) return 0;

  if(quantity > BUFFER_LENGTH)
  {
    quantity = BUFFER_LENGTH;
  }

  uint8_t read = twisw_readFrom(address, rxBuffer, quantity, sendStop);

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_rxBuffLock);
#endif
  rxBufferIndex = 0;
  rxBufferLength = read;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_rxBuffLock);
#endif

  return read;
}

uint8_t TwoWireLEGO::requestFrom(uint8_t address, uint8_t quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWireLEGO::requestFrom(int address, int quantity)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)true);
}

uint8_t TwoWireLEGO::requestFrom(int address, int quantity, int sendStop)
{
  return requestFrom((uint8_t)address, (uint8_t)quantity, (uint8_t)sendStop);
}

void TwoWireLEGO::beginTransmission(uint8_t address)
{
#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_txBuffLock);
#endif
  transmitting   = 1;
  txAddress     = address;

  txBufferIndex  = 0;
  txBufferLength = 0;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_txBuffLock);
#endif
}

void TwoWireLEGO::beginTransmission(int address)
{
  beginTransmission((uint8_t)address);
}

uint8_t TwoWireLEGO::endTransmission(uint8_t sendStop)
{
  int8_t ret = twisw_writeTo(txAddress, txBuffer, txBufferLength, sendStop);

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_txBuffLock);
#endif
  txBufferIndex  = 0;
  txBufferLength = 0;

  transmitting   = 0;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_txBuffLock);
#endif
  return ret;
}

uint8_t TwoWireLEGO::endTransmission(void)
{
  return endTransmission(true);
}

size_t TwoWireLEGO::write(uint8_t data)
{
  int txLenght;

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_txBuffLock);
#endif
  txLenght = txBufferLength;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_txBuffLock);
#endif
  
  if(txLenght >= BUFFER_LENGTH)
  {
    setWriteError();
    return 0;
  }

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_txBuffLock);
#endif
  txBuffer[txBufferIndex] = data;
  ++txBufferIndex;

  txBufferLength = txBufferIndex;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_txBuffLock);
#endif
  return 1;
}

size_t TwoWireLEGO::write(const uint8_t *data, size_t quantity)
{
  for(size_t i = 0; i < quantity; ++i) write(data[i]);
  return quantity;
}

int TwoWireLEGO::available(void)
{
  int ret;

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_rxBuffLock);
#endif
  ret = rxBufferLength - rxBufferIndex;
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_rxBuffLock);
#endif
  return ret;
}

int TwoWireLEGO::read(void)
{
  int value = -1;

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_rxBuffLock);
#endif
  if(rxBufferIndex < rxBufferLength)
  {
    value = rxBuffer[rxBufferIndex];
    ++rxBufferIndex;
  }
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_rxBuffLock);
#endif
  return value;
}

int TwoWireLEGO::peek(void)
{
  int value = -1;

#if defined (DMP_LINUX)
  pthread_spin_lock(&sw_rxBuffLock);
#endif
  if(rxBufferIndex < rxBufferLength)
  {
    value = rxBuffer[rxBufferIndex];
  }
#if defined (DMP_LINUX)
  pthread_spin_unlock(&sw_rxBuffLock);
#endif
  return value;
}

void TwoWireLEGO::flush(void)
{
  // XXX: to be implemented.
}

#if defined (DMP_LINUX)
bool TwoWireLEGO::send(uint8_t addr, uint8_t* data, int datasize) {
	int i;
	bool err = false;
	
	pthread_spin_lock(&sw_multiWireLock);
	
	WireLEGO.beginTransmission(addr);
	for(i=0; i<datasize; i++) WireLEGO.write(data[i]);
	if(WireLEGO.endTransmission() != 0) {printf("%s error\n", __FUNCTION__); err = true;}
	
	pthread_spin_unlock(&sw_multiWireLock);
	
	return (err == true) ? false : true;
}

bool TwoWireLEGO::receive(uint8_t addr, uint8_t* buf, uint8_t bufsize) {
	int i, read;
	bool err = false;
	if(buf == NULL || bufsize == 0) return 0;
	
	pthread_spin_lock(&sw_multiWireLock);
	
	read = WireLEGO.requestFrom(addr, bufsize);
	if(read < bufsize) {printf("The  bytes read by I2C are less than input buff size\n"); err = true;}
	for(i=0; i<read; i++) buf[i] = WireLEGO.read();
	
	pthread_spin_unlock(&sw_multiWireLock);
	
	return (err == true) ? false : true;
}

bool TwoWireLEGO::sensorRead(uint8_t addr, uint8_t cmd, uint8_t* buf, uint8_t bufsize) {
	return sensorReadEX(addr, &cmd, 1, buf, bufsize);
}
bool TwoWireLEGO::sensorReadEX(uint8_t addr, uint8_t* cmds, int cmdsize, uint8_t* buf, uint8_t bufsize) {
	int i, read;
	bool err = false;

	pthread_spin_lock(&sw_multiWireLock);

	WireLEGO.beginTransmission(addr);
	for(i=0; i<cmdsize; i++) WireLEGO.write(cmds[i]);
	if(WireLEGO.endTransmission(false) != 0) {printf("%s error\n", __FUNCTION__); err = true;} // send restart 
	read = WireLEGO.requestFrom(addr, bufsize);
	if(read < bufsize) {printf("The  bytes read by I2C are less than input buff size\n"); err = true;}
	for(i=0; i<read; i++) buf[i] = WireLEGO.read();

	pthread_spin_unlock(&sw_multiWireLock);
	
	return (err == true) ? false : true;
}
#endif


// Preinstantiate Objects //////////////////////////////////////////////////////

TwoWire Wire = TwoWire();
TwoWireLEGO WireLEGO = TwoWireLEGO();