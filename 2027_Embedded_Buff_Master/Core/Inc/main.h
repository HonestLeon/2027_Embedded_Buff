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
#include "stm32f4xx_hal.h"

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

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define KEY1_Pin GPIO_PIN_1
#define KEY1_GPIO_Port GPIOA
#define KEY1_EXTI_IRQn EXTI1_IRQn
#define KEY2_Pin GPIO_PIN_2
#define KEY2_GPIO_Port GPIOA
#define KEY2_EXTI_IRQn EXTI2_IRQn
#define KEY3_Pin GPIO_PIN_3
#define KEY3_GPIO_Port GPIOA
#define KEY3_EXTI_IRQn EXTI3_IRQn
#define KEY4_Pin GPIO_PIN_4
#define KEY4_GPIO_Port GPIOA
#define KEY4_EXTI_IRQn EXTI4_IRQn
#define KEY5_Pin GPIO_PIN_5
#define KEY5_GPIO_Port GPIOA
#define KEY5_EXTI_IRQn EXTI9_5_IRQn
#define KEY6_Pin GPIO_PIN_6
#define KEY6_GPIO_Port GPIOC
#define KEY6_EXTI_IRQn EXTI9_5_IRQn
#define KEY7_Pin GPIO_PIN_7
#define KEY7_GPIO_Port GPIOC
#define KEY7_EXTI_IRQn EXTI9_5_IRQn
#define KEY8_Pin GPIO_PIN_8
#define KEY8_GPIO_Port GPIOC
#define KEY8_EXTI_IRQn EXTI9_5_IRQn
#define KEY9_Pin GPIO_PIN_9
#define KEY9_GPIO_Port GPIOC
#define KEY9_EXTI_IRQn EXTI9_5_IRQn
#define KEY10_Pin GPIO_PIN_10
#define KEY10_GPIO_Port GPIOC
#define KEY10_EXTI_IRQn EXTI15_10_IRQn
#define BOARD_ID1_Pin GPIO_PIN_11
#define BOARD_ID1_GPIO_Port GPIOC
#define BOARD_ID2_Pin GPIO_PIN_12
#define BOARD_ID2_GPIO_Port GPIOC
#define SW_BUFF_Pin GPIO_PIN_2
#define SW_BUFF_GPIO_Port GPIOD
#define SW_RUN_Pin GPIO_PIN_3
#define SW_RUN_GPIO_Port GPIOB
#define SW_COLOR_Pin GPIO_PIN_4
#define SW_COLOR_GPIO_Port GPIOB
#define SW_DEBUG_Pin GPIO_PIN_5
#define SW_DEBUG_GPIO_Port GPIOB
#define BOARD_ID3_Pin GPIO_PIN_9
#define BOARD_ID3_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
