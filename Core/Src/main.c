/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// Un-comment to bypass analog entries and use constant fixed values
//#define FIXED_FREQ 200000 // Max 720_000
//#define FIXED_DUTY 60

// define min max frequency reachable by potentiometer
#define FREQ_MIN 1
#define FREQ_MAX 300 // Max 720_000

// define min max analog values to avoid potentiometer noise and unreachable range
#define POT_MIN 160
#define POT_MAX 3900 // Max 4096

#define ADC_PW_MOY 10 // Min 1, Max 14 due to DMA NDT limit, 8 means an average by 256.
// /!\ ADC_PW_MOY higher than 10 take to much place in RAM and trigger error at compilation

#define LED_INTERVAL 250

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define FREQ_BDW (FREQ_MAX - FREQ_MIN)
#define POT_INTER (POT_MAX - POT_MIN)

#define ADC_NB_MOY (1 << ADC_PW_MOY)
#define ADC_BUF_SIZE (2 * ADC_NB_MOY)

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint32_t led_last_time;

uint16_t adc_val[ADC_BUF_SIZE];
uint16_t pot1 = 0;
uint16_t pot2 = 0;

uint32_t freq = 1;
uint16_t ducy = 50;

uint32_t ccr = 0;
uint32_t arr = 0;
uint32_t clk = 0;

uint8_t ipsc = 25; // start with 72 divider
uint16_t psc[67] = { 1,    2,    3,    4,    5,    6,    8,    9,    10,   12, // 72M timer clock dividers
                     15,   16,   18,   20,   24,   25,   30,   32,   36,   40,
                     45,   48,   50,   60,   64,   72,   75,   80,   90,   96,
                     100,  120,  125,  128,  144,  150,  160,  180,  192,  200,
                     225,  240,  250,  256,  288,  300,  320,  360,  375,  384,
                     400,  450,  480,  500,  512,  576,  600,  625,  640,  720,
                     750,  768,  800,  900,  960,  1000, 1125 }; // stop here because enough for minimum frequency 1Hz

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void DMA_ADC_init(void);
void get_pot_average(void);
void crop_values(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  HAL_ADCEx_Calibration_Start(&hadc1);
  // HAL would prefer a 32bit adc_val, but 16bits is enough as ADC is 12bit
  // Also, 32bit would take more place in RAM, limiting average to 512
  HAL_ADC_Start_DMA(&hadc1, adc_val, ADC_BUF_SIZE);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
  HAL_Delay(1);	// shift signals
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
  HAL_Delay(1); // shift signals
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {

#if defined(FIXED_FREQ) && defined(FIXED_DUTY)
	  freq = FIXED_FREQ;
	  ducy = FIXED_DUTY;
#else
	  get_pot_average();
	  crop_values();

	  freq = ((float)pot1 / POT_INTER) * FREQ_BDW + FREQ_MIN;
	  ducy = ((float)pot2 / POT_INTER) * 100;
	  if (freq == 0)
		  freq = 1;
	  if (ducy > 100)
		  ducy = 100;
#endif

	  // Search timer setup to fit expected frequency
	  // TIM_CLK = APB_TIM_CLOCK / PRESCALAR
	  // FREQ = TIM_CLOCK / ARR
	  // DUTY (%) = (CCRx / ARR) * 100
	  while (1) {
		  clk = 72000000 / psc[ipsc];
		  arr = clk / freq;
		  if (arr < 100) { // arr > 100 to be able to process any duty-cycle on it
			  ipsc--;
			  continue;
		  } else if (arr > 65535) { // arr is 16 bit register
			  ipsc++;
			  continue;
		  }
		  ccr = (ducy * arr) / 100;
		  break;
	  }

	  TIM1->CCR1 = ccr & 0xFFFF;
	  TIM1->CCR2 = ccr & 0xFFFF;
	  TIM1->CCR3 = ccr & 0xFFFF;
	  TIM1->CCR4 = ccr & 0xFFFF;
	  TIM1->ARR = (arr-1) & 0xFFFF;
	  TIM1->PSC = psc[ipsc]-1;

//	  TIM2->CCR1 = ccr & 0xFFFF;
//	  TIM2->CCR2 = ccr & 0xFFFF;
	  TIM2->CCR3 = ccr & 0xFFFF;
	  TIM2->CCR4 = ccr & 0xFFFF;
	  TIM2->ARR = (arr-1) & 0xFFFF;
	  TIM2->PSC = psc[ipsc]-1;

	  TIM3->CCR1 = ccr & 0xFFFF;
	  TIM3->CCR2 = ccr & 0xFFFF;
//	  TIM3->CCR3 = ccr & 0xFFFF;
//	  TIM3->CCR4 = ccr & 0xFFFF;
	  TIM3->ARR = (arr-1) & 0xFFFF;
	  TIM3->PSC = psc[ipsc]-1;

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	  if (HAL_GetTick() - led_last_time > LED_INTERVAL) {
		  led_last_time = HAL_GetTick();
		  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
	  }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void get_pot_average(void) {
	uint32_t pot1_c = 0;
	uint32_t pot2_c = 0;
	for (int i = 0; i < ADC_NB_MOY; i+=2) {
		pot1_c += adc_val[i];
		pot2_c += adc_val[i+1];
	}
	pot1 = (uint16_t)(pot1_c >> (ADC_PW_MOY-1));
	pot2 = (uint16_t)(pot2_c >> (ADC_PW_MOY-1));
}

void crop_values(void) {
	if (pot1 < POT_MIN)
		pot1 = POT_MIN;
	if (pot2 < POT_MIN)
		pot2 = POT_MIN;
	if (pot1 > POT_MAX)
		pot1 = POT_MAX;
	if (pot2 > POT_MAX)
		pot2 = POT_MAX;
	pot1 -= POT_MIN;
	pot2 -= POT_MIN;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
