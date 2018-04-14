/**
 * @author  Nickel_Liang
 * @date    2018-04-12
 * @file    bsp_can.c
 * @brief   Board Support Package for CAN bus
 * @log     2018-04-13 Nickel_Liang
 */

#include "bsp_can.h"

void CAN_transmit(CAN_HandleTypeDef* hcan, uint16_t id, int16_t msg1, int16_t msg2, int16_t msg3, int16_t msg4) {
	hcan->pTxMsg->StdId = id;
	hcan->pTxMsg->IDE = CAN_ID_STD;
	hcan->pTxMsg->RTR = CAN_RTR_DATA;
	hcan->pTxMsg->DLC = 0x08;
	hcan->pTxMsg->Data[0] = msg1 >> 8; 	//Higher 8 bits of ESC 1
	hcan->pTxMsg->Data[1] = msg1;		//Lower 8 bits of ESC 1
	hcan->pTxMsg->Data[2] = msg2 >> 8;
	hcan->pTxMsg->Data[3] = msg2;
	hcan->pTxMsg->Data[4] = msg3 >> 8;
	hcan->pTxMsg->Data[5] = msg3;
	hcan->pTxMsg->Data[6] = msg4 >> 8;
	hcan->pTxMsg->Data[7] = msg4;
	HAL_CAN_Transmit(hcan, 1000);
}

void CAN1_init(void) {
	RM_CAN_FilterConfiguration(&hcan1);   //Initialize filter 0
	if (HAL_CAN_Receive_IT(&hcan1, CAN_FIFO0) != HAL_OK) {
		Error_Handler();
    }
}

void CAN2_init(void) {
	RM_CAN_FilterConfiguration(&hcan2);   //Initialize filter 0
	if (HAL_CAN_Receive_IT(&hcan2, CAN_FIFO0) != HAL_OK) {
		Error_Handler();
    }
}

static void CAN_filter_config(CAN_HandleTypeDef* hcan) {
	CAN_FilterConfTypeDef	CAN_FilterConfigStructure;

	static CanTxMsgTypeDef	Tx1Message;	//Allocate memory for data storage
	static CanRxMsgTypeDef 	Rx1Message;
	static CanTxMsgTypeDef	Tx2Message;
	static CanRxMsgTypeDef 	Rx2Message;

	CAN_FilterConfigStructure.FilterIdHigh = 0x0000;
	CAN_FilterConfigStructure.FilterIdLow = 0x0000;
	CAN_FilterConfigStructure.FilterMaskIdHigh = 0x0000;
	CAN_FilterConfigStructure.FilterMaskIdLow = 0x0000;
	CAN_FilterConfigStructure.FilterFIFOAssignment = CAN_FilterFIFO0;
	CAN_FilterConfigStructure.FilterMode = CAN_FILTERMODE_IDMASK;
	CAN_FilterConfigStructure.FilterScale = CAN_FILTERSCALE_32BIT;
	CAN_FilterConfigStructure.FilterActivation = ENABLE;
	CAN_FilterConfigStructure.BankNumber = 14;	//CAN1 and CAN2 split all 28 filters

	if (hcan == &hcan1) {
		CAN_FilterConfigStructure.FilterNumber = 0; //Master CAN1 get filter 0-13
		hcan->pTxMsg = &Tx1Message;
		hcan->pRxMsg = &Rx1Message;
	} else if (hcan == &hcan2) {
		CAN_FilterConfigStructure.FilterNumber = 14; //Slave CAN2 get filter 14-27
		hcan->pTxMsg = &Tx2Message;
		hcan->pRxMsg = &Rx2Message;
	}

	if (HAL_CAN_ConfigFilter(hcan, &CAN_FilterConfigStructure) != HAL_OK) {
		Error_Handler();
    }
}

static void RM_CAN_GetChassisData(CAN_HandleTypeDef* hcan, motor_measure_t *ptr) {
	//3508 DATA[0]DATA[1]
	ptr->lastAngle 		= ptr->angle;
	ptr->angle 			= (uint16_t)(hcan->pRxMsg->Data[0]<<8 | hcan->pRxMsg->Data[1]);
	if (ptr->angle - ptr->lastAngle > 4096)
		ptr->roundCnt --;
	else if (ptr->angle - ptr->lastAngle < -4096)
		ptr->roundCnt ++;
	ptr->totalAngle 	= ptr->roundCnt * 8192 + ptr->angle - ptr->offsetAngle;
	//3508 DATA[2]DATA[3]
	ptr->speedRPM 		= (int16_t)(hcan->pRxMsg->Data[2]<<8 | hcan->pRxMsg->Data[3]);
	//3508 DATA[4]DATA[5]
	ptr->torqueCurrent	= (int16_t)(hcan->pRxMsg->Data[4]<<8 | hcan->pRxMsg->Data[5]);
	//3508 DATA[6]
	ptr->temp 			= (uint8_t)hcan->pRxMsg->Data[6];
	//Set unused data to prevent bug
	ptr->givenCurrent	= 0; //3508 don't have given current feedback
}

/******************************************************************************
	Input
		hcan, motor ptr
	Output
	Description
		For 6623
			Receive and change data in moto struc
	Log
		11/24/17 Nickel Liang	First Draft
		12/05/17 Nickel Liang	Add more comment, make it static
 ******************************************************************************/
static void RM_CAN_GetGimbalData(CAN_HandleTypeDef* hcan, motor_measure_t *ptr) {
	//6623 DATA[0]DATA[1]
	ptr->lastAngle 		= ptr->angle;
	ptr->angle 			= (uint16_t)(hcan->pRxMsg->Data[0]<<8 | hcan->pRxMsg->Data[1]);
	if (ptr->angle - ptr->lastAngle > 4096)
		ptr->roundCnt --;
	else if (ptr->angle - ptr->lastAngle < -4096)
		ptr->roundCnt ++;
	ptr->totalAngle 	= ptr->roundCnt * 8192 + ptr->angle - ptr->offsetAngle;
	//6623 DATA[2]DATA[3]
	ptr->torqueCurrent 	= (int16_t)(hcan->pRxMsg->Data[2]<<8 | hcan->pRxMsg->Data[3]);
	//6623 DATA[4]DATA[5]
	ptr->givenCurrent	= (int16_t)(hcan->pRxMsg->Data[4]<<8 | hcan->pRxMsg->Data[5]);
	//Set unused data to prevent bug
	ptr->speedRPM		= 0; //6623 don't have speed and temperature feedback
	ptr->temp			= 0;
}

/******************************************************************************
	Input
		hcan, motor ptr
	Output
	Description
		For M3508 P19 Motor with C620 ESC
			Receive and set initial angle
		For 6623
			Receive and set initial angle
	Log
		11/24/17 Nickel Liang	First Draft
		12/05/17 Nickel Liang	Make it static
 ******************************************************************************/
static void RM_CAN_GetOffset(CAN_HandleTypeDef* hcan, motor_measure_t *ptr) {
	//3508 DATA[0]DATA[1], 6623 DATA[0]DATA[1]
	ptr->angle 		 = (uint16_t)(hcan->pRxMsg->Data[0]<<8 | hcan->pRxMsg->Data[1]);
	ptr->offsetAngle = ptr->angle;
}

/******************************************************************************
	Input
		hcan
	Output
	Description
		DO NOT DECLARE THIS FUNCTION
		Rx CALLBACK function, declared by HAL, no need to declare here
		This is the function that will actually change the data in motor_measure_t
		structs. This function will be called when the system finish interrupt
		and come back to RTOS kernal.
	Log
		11/24/17 Nickel Liang	First Draft
		12/05/17 Nickel Liang	Add more comment
 ******************************************************************************/
void HAL_CAN_RxCpltCallback(CAN_HandleTypeDef* hcan) {
	switch (hcan->pRxMsg->StdId) {
		case CAN_3508_1_RX:
		case CAN_3508_2_RX:
		case CAN_3508_3_RX:
		case CAN_3508_4_RX:
			{ //Delete this bracket will cause compile error
				static uint8_t i;
				i = hcan->pRxMsg->StdId - CAN_3508_1_RX;
				// First 50 messages are used to set the initial offset. Message @ 1kHz, 50 messages = 50ms
				// Every one is using 50 here. So I just use 50. Will try smaller number later
				motorChassis[i].msgCnt++ <= 50 ? RM_CAN_GetOffset(hcan, &motorChassis[i]) : RM_CAN_GetChassisData(hcan, &motorChassis[i]);
				break;
			}
		case CAN_3508_5_RX:
		case CAN_3508_6_RX:
			{
				static uint8_t i;
				i = hcan->pRxMsg->StdId - CAN_3508_5_RX;
				motorLauncher[i].msgCnt++ <= 50 ? RM_CAN_GetOffset(hcan, &motorLauncher[i]) : RM_CAN_GetChassisData(hcan, &motorLauncher[i]);
				break;
			}
		case CAN_3508_7_RX:
			motorPoke.msgCnt++ <= 50 ? RM_CAN_GetOffset(hcan, &motorPoke) : RM_CAN_GetChassisData(hcan, &motorPoke);
			break;
		case CAN_PITCH_RX:
			motorPitch.msgCnt++ <= 50 ? RM_CAN_GetOffset(hcan, &motorPitch) : RM_CAN_GetGimbalData(hcan, &motorPitch);
			break;
		case CAN_YAW_RX:
			motorYaw.msgCnt++ <= 50 ? RM_CAN_GetOffset(hcan, &motorYaw) : RM_CAN_GetGimbalData(hcan, &motorYaw);
			break;
	}
    // Reset CAN receive interrupt to prevent bug
	__HAL_CAN_ENABLE_IT(&hcan1, CAN_IT_FMP0);
	__HAL_CAN_ENABLE_IT(&hcan2, CAN_IT_FMP0);
}
