/*
 * STM32 무게/터치/라즈베리파이 안전 시스템
 * - 로드셀(HX711)로 무게 측정
 * - 터치 센서 미감지 시간에 따라 경고(LED/진동)
 * - 라즈베리파이 신호로 부저 제어
 *
 * 이 파일은 STM32CubeIDE로 생성된 main.c의 해당 USER CODE 영역에
 * 나눠서 붙여넣는 것을 전제로 정리한 코드입니다. (Includes/Init 함수 등
 * CubeMX 자동 생성 코드는 포함하지 않았습니다.)
 *
 * 하드웨어 핀 매핑
 *  - 로드셀(HX711): DT = PA5, SCK = PA7
 *  - 터치 센서 입력: touch_in_Pin (GPIOE)
 *  - 라즈베리파이 입력: rpi_Pin (GPIOE)
 *  - LED_1_Pin (GPIOE): 터치 센서 관련 경고
 *  - LED_2_Pin (GPIOE): 로드셀 관련 표시
 *  - LED_3_Pin (GPIOE): 로드셀 경고 LED (깜빡이 패턴)
 *  - vibrator_out_Pin: 진동 모터
 *  - BUZZER_Pin (GPIOB): 부저 (Active-Low, LOW면 ON)
 *  - TIM2: 100ms 주기 인터럽트 (시계/카운터, 로직 전반 시간 기반)
 *  - TIM3: 마이크로초(us) 딜레이용 (HX711 클럭 타이밍)
 */

/* ============================================================
 * 전역 변수
 * ============================================================ */
uint8_t  flag          = 1;   // 처음 1번만 사용하는 플래그 (초기 5초 후 영점 잡기용)
uint16_t x             = 0;   // 외부 인터럽트(EXTI)로 토글되는 플래그 (로드셀 관련 on/off)
volatile uint8_t onesecond_key = 0; // TIM2 인터럽트에서 쓰는 100ms 틱 키

/* 시간 카운터 */
uint16_t tenseccount = 0; // 0~9, 100ms 단위 (10이 되면 1초 증가)
uint16_t seccount    = 0; // 초(0~59)
uint16_t mincount    = 0; // 분(0~59)
uint16_t hourcount    = 0; // 시(0~23)

/* 상태 카운터 */
uint16_t touchcount = 0; // 터치 미감지 시간 (0.1s 단위)
uint8_t  touchflag  = 0; // 터치 감지 여부 플래그 (0: 터치 중, 1: 터치 없음)
uint16_t vicount    = 0; // 진동 패턴용 카운터

uint16_t roadcount   = 0; // 로드셀 조건 유지 시간 카운터
uint8_t  road_active = 0; // 로드셀 조건 만족 상태 플래그 (0/1)

/* 로드셀 영점 잡기/보정 */
float    weight_boom = 0;       // 초기 5초 동안의 평균값을 기준으로 하는 영점
uint32_t tare = 7907849;        // 기준값
float    knownOriginal = 190000; // kg 단위(칼리브레이션용)
float    knownHX711    = 5000;   // 기준 무게(예: 5kg)
float    weight;                // 실제 사용하는 보정된 무게
float    weight_1;              // 로드셀에서 읽은 현재 값(영점 보정 전/후 사용)

/* ============================================================
 * 3. HX711 관련 함수들
 * ============================================================ */

/* 3-1. 마이크로초 딜레이 (TIM3 사용) */
void microDelay(uint16_t delay) {
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    while (__HAL_TIM_GET_COUNTER(&htim3) < delay);
}

/* 3-2. HX711에서 24비트 값 읽기 */
int32_t getHX711(void) {
    uint32_t data = 0;
    uint32_t startTime = HAL_GetTick();

    // DT가 LOW가 될 때까지 대기 (최대 200ms)
    while (HAL_GPIO_ReadPin(DT_PORT, DT_PIN) == GPIO_PIN_SET) {
        if (HAL_GetTick() - startTime > 200) return 0;
    }

    // 24비트 데이터 읽기
    for (int8_t len = 0; len < 24; len++) {
        HAL_GPIO_WritePin(SCK_PORT, SCK_PIN, GPIO_PIN_SET);
        microDelay(1);
        data = data << 1;
        HAL_GPIO_WritePin(SCK_PORT, SCK_PIN, GPIO_PIN_RESET);
        microDelay(1);
        if (HAL_GPIO_ReadPin(DT_PORT, DT_PIN) == GPIO_PIN_SET)
            data++;
    }

    // 부호비트 처리
    data = data ^ 0x800000;

    // 게인 설정을 위한 추가 클럭 (1회)
    HAL_GPIO_WritePin(SCK_PORT, SCK_PIN, GPIO_PIN_SET);
    microDelay(1);
    HAL_GPIO_WritePin(SCK_PORT, SCK_PIN, GPIO_PIN_RESET);
    microDelay(1);

    return data;
}

/* 3-3. 로드셀 값 50회 평균 -> kg 단위로 보정된 무게 반환 */
float weigh(void) {
    int32_t total = 0;
    int32_t samples = 50;
    float kilogram;
    float coefficient;

    // 50회 샘플 평균
    for (uint16_t i = 0; i < samples; i++) {
        total += getHX711();
    }

    int32_t average = (int32_t)(total / samples);

    // 보정 계수 (칼리브레이션)
    coefficient = -(knownOriginal / knownHX711) / 1000000;
    kilogram = (float)(average - tare) * coefficient;

    return kilogram;
}

/* ============================================================
 * 4. 라즈베리파이 신호 안정화 함수
 * (5번 읽어서 3번 이상 HIGH면 HIGH로 인정하는 디바운스/안정화 처리)
 * ============================================================ */
static GPIO_PinState read_rpi_stable(void) {
    uint8_t high = 0;
    for (int i = 0; i < 5; ++i) {
        if (HAL_GPIO_ReadPin(rpi_GPIO_Port, rpi_Pin) == GPIO_PIN_SET) {
            high++;
        }
    }
    return (high >= 3) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/* ============================================================
 * 5. main() 흐름 (초기 설정 + 메인 무한 루프)
 * ============================================================ */

/* 5-1. 초기 설정 (main() 내부, while(1) 이전) */
/*
HAL_Init();
SystemClock_Config();
MX_GPIO_Init();
MX_TIM2_Init();
MX_TIM3_Init();

// TIM2 인터럽트 시작 (100ms 주기)
HAL_TIM_Base_Start_IT(&htim2);

// TIM3 시작 (딜레이용)
HAL_TIM_Base_Start(&htim3);

// HX711 초기 클럭 토글
HAL_GPIO_WritePin(SCK_PORT, SCK_PIN, GPIO_PIN_SET);   HAL_Delay(10);
HAL_GPIO_WritePin(SCK_PORT, SCK_PIN, GPIO_PIN_RESET); HAL_Delay(10);
*/

/* 5-2. 메인 무한 루프(while(1)) */
while (1) {
    // (1) 초기 5초 동안 영점(weight_boom) 맞추기
    if (flag == 1) {
        if (seccount > 5) {   // 5초가 지나면
            weight_boom = weight_1;
            flag = 0;          // 한 번만 수행
        }
    }

    weight_1 = weigh() - weight_boom; // 현재 무게에서 영점값 빼기

    // (4) 터치 센서 처리 및 진동/LED_1 제어
    uint8_t touch = HAL_GPIO_ReadPin(touch_in_GPIO_Port, touch_in_Pin);
    // touch == 0 -> 터치 미감지 상태로 사용 (보드 회로에 따라 반대일 수 있음, 지금은 0이 "안 잡고 있음")

    if (touch == 0) {
        touchflag = 1;

        // 5초~15초 미터치: LED_1 깜빡이기 (0.5s ON / 0.5s OFF)
        if (touchcount > 50 && touchcount < 150) {
            if (tenseccount <= 5) {
                HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);
            } else if (tenseccount > 5) {
                HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);
            }
        }

        // 15초 이상 미터치: LED_1 같은 패턴 + 진동 패턴 추가
        if (touchcount >= 150) {
            vicount++;

            // LED_1: 위와 동일 0.5초 ON / 0.5초 OFF
            if (tenseccount <= 5)
                HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);
            else
                HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);

            // 진동: vicount 기반으로 (1s 정도 ON/OFF 반복)
            if (vicount <= 2) {
                HAL_GPIO_WritePin(vibrator_out_GPIO_Port, vibrator_out_Pin, GPIO_PIN_SET);   // 진동 ON
            } else if (vicount > 2 && vicount < 4) {
                HAL_GPIO_WritePin(vibrator_out_GPIO_Port, vibrator_out_Pin, GPIO_PIN_RESET); // 진동 OFF
            } else if (vicount >= 4) {
                vicount = 0;
            }
        }
    }
    // 4-2) 터치 감지 상태(touch == 1)
    else {
        touchcount = 0;
        vicount = 0;
        touchflag = 0;
        HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(vibrator_out_GPIO_Port, vibrator_out_Pin, GPIO_PIN_RESET);
    }

    // (5) 라즈베리파이 -> 부저 제어 (Active-Low)
    GPIO_PinState rpi_val = read_rpi_stable(); // 안정화된 입력 읽기

    if (rpi_val == GPIO_PIN_SET) {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET); // BUZZER ON
    } else {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);   // BUZZER OFF
    }
}

/* ============================================================
 * 6. 인터럽트 콜백들
 * ============================================================ */

/* 6-1. 외부 인터럽트: EXTI_Pin에서 인터럽트가 들어올 때마다 x 값을 0<->1로 토글
 *      (메인 루프에서 로드셀 관련 LED 동작을 켜고 끄는 스위치 역할) */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (x == 0) {
        x = 1;   // 로드셀 경고/동작 시작
    } else {
        x = 0;   // 로드셀 경고/동작 종료
    }
}

/* 6-2. 타이머 인터럽트: TIM2가 100ms마다 발생
 *      - 터치 미감지 시간이면 touchcount++
 *      - tenseccount++ 해서 10번(=1초)이 되면 seccount++
 *      - x == 1 이고 weight_1 > 15kg일 때 -> road_active = 1 -> roadcount++
 *      - 초/분/시 카운터 갱신
 *      즉, 시스템의 모든 "시간 기반 조건"은 이 타이머에서 카운터로 처리됩니다. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        onesecond_key = 1;

        if (onesecond_key == 1) {
            if (touchflag == 1) { touchcount++; }
            else { touchcount = 0; }

            onesecond_key = 0;

            /* 0.1초 단위 틱 */
            tenseccount++;
            if (tenseccount >= 10) {
                tenseccount = 0;
                seccount++;

                // 로드셀 조건 체크
                if (x == 1 && weight_1 > 15) {
                    road_active = 1;
                } else {
                    roadcount = 0;
                    road_active = 0;
                }

                if (road_active == 1) { roadcount++; }

                // 시, 분, 초 카운터
                if (seccount >= 60) {
                    seccount = 0;
                    mincount++;
                    if (mincount >= 60) {
                        mincount = 0;
                        hourcount++;
                        if (hourcount >= 24) hourcount = 0;
                    }
                }
            }
        }
    }
}
