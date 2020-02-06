#ifndef __CONTROLLER_STREAM_UART_DECODER_H__
#define __CONTROLLER_STREAM_UART_DECODER_H__

#include <cstdint>
#include <queue>

typedef struct
{
	uint32_t timestamp;
	uint16_t phase;
	uint16_t torque;
	uint16_t motor_voltage;
	int16_t  phaseACurrent;
	int16_t  phaseBCurrent;
	int16_t  phaseCCurrent;

}  __attribute__((packed)) StreamSample;

class  ControllerStreamUartDecoder
{
public:

	ControllerStreamUartDecoder();
	virtual ~ControllerStreamUartDecoder(){};
	void processBytes(uint8_t *buffer, unsigned int nbBytes);

	bool getDecodedSample(StreamSample *sample);

private:
	StreamSample currentDecodedSample;
	bool isCurrentSampleValid = false;

	std::queue<StreamSample> decodedSampleQueue;

	void synchroLookUp(uint8_t byte);
	void getRemainingData(uint8_t byte);

	typedef void (ControllerStreamUartDecoder::*stateFunction)(uint8_t byte);
	stateFunction currentState;
};

#endif //__CONTROLLER_STREAM_UART_DECODER_H__
