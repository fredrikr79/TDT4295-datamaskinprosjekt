/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void MX_SDMMC1_SD_Init(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_R_Pin GPIO_PIN_2
#define LED_R_GPIO_Port GPIOE
#define USER_BUTTON_Pin GPIO_PIN_13
#define USER_BUTTON_GPIO_Port GPIOC
#define USER_BUTTON_EXTI_IRQn EXTI13_IRQn
#define FPGA_READY_Pin GPIO_PIN_0
#define FPGA_READY_GPIO_Port GPIOF
#define FPGA_READY_EXTI_IRQn EXTI0_IRQn
#define FPGA_ACK_Pin GPIO_PIN_1
#define FPGA_ACK_GPIO_Port GPIOF
#define FPGA_SYNC_Pin GPIO_PIN_2
#define FPGA_SYNC_GPIO_Port GPIOF
#define FPGA_SYNC_EXTI_IRQn EXTI2_IRQn
#define FPGA_RESET_Pin GPIO_PIN_3
#define FPGA_RESET_GPIO_Port GPIOF
#define Drive_VBUS_Pin GPIO_PIN_4
#define Drive_VBUS_GPIO_Port GPIOA
#define fault_signal_Pin GPIO_PIN_5
#define fault_signal_GPIO_Port GPIOA
#define FPGA_PROGRAM_B_Pin GPIO_PIN_0
#define FPGA_PROGRAM_B_GPIO_Port GPIOD
#define FPGA_DONE_Pin GPIO_PIN_1
#define FPGA_DONE_GPIO_Port GPIOD
#define LED_B_Pin GPIO_PIN_0
#define LED_B_GPIO_Port GPIOE
#define LED_G_Pin GPIO_PIN_1
#define LED_G_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
