<h1 align="center">🚗💤 다중 센서 기반 졸음운전 방지 시스템</h1>
<p align="center">Multi-sensor-based Drowsy Driving Prevention System</p>

<p align="center">
  <img src="https://img.shields.io/badge/Raspberry_Pi-A22846?style=flat-square&logo=raspberrypi&logoColor=white" />
  <img src="https://img.shields.io/badge/STM32-03234B?style=flat-square&logo=stmicroelectronics&logoColor=white" />
  <img src="https://img.shields.io/badge/Python-3776AB?style=flat-square&logo=python&logoColor=white" />
  <img src="https://img.shields.io/badge/OpenCV-5C3EE8?style=flat-square&logo=opencv&logoColor=white" />
  <img src="https://img.shields.io/badge/MediaPipe-00A98F?style=flat-square" />
  <img src="https://img.shields.io/badge/C-00599C?style=flat-square&logo=c&logoColor=white" />
</p>

---

## 📖 개요

카메라 기반 눈 감김 인식과 조향핸들 터치/로드셀 센서를 결합해 운전자의 졸음·이탈 상태를 다각도로 판단하고, 위험 상황에서 부저·LED·진동으로 경고하는 시스템입니다. 호서대학교 졸업작품으로 진행했습니다.

## 🏗️ 시스템 아키텍처

```
[Raspberry Pi + Camera]                          [STM32]
 OpenCV + MediaPipe FaceMesh                       HX711 로드셀 (핸들 그립압)
 눈 감김(EAR) 임계값 판정                            터치 센서 (핸들 이탈 감지)
        │                                              │
        │  GPIO(BCM17) HIGH/LOW                        │
        └───────────────────────────▶  STM32 GPIO 입력  │
                                        5회 샘플링 디바운싱
                                        (3회 이상 HIGH → 인정)
                                              │
                                              ▼
                              부저 ON/OFF + LED 경고 + 진동 모터
```

- **Raspberry Pi**: 카메라 영상에서 MediaPipe FaceMesh로 눈 좌표(위/아래 랜드마크)를 추출, 눈뜸 정도(EAR 유사 지표)를 계산해 일정 시간 이상 눈이 감기면 GPIO를 HIGH로 출력
- **STM32**: 라즈베리파이 신호를 5회 샘플링해 3회 이상 HIGH일 때만 실제 HIGH로 인정하는 디바운싱 로직으로 노이즈에 의한 오작동 방지, 로드셀(HX711)로 핸들 그립압을 측정하고 터치 센서로 핸들 이탈 여부를 함께 판단해 부저/LED/진동으로 경고

## ✅ 담당 역할

| 영역 | 내용 |
|---|---|
| 컴퓨터 비전 | OpenCV·MediaPipe FaceMesh 기반 졸음 감지 알고리즘 적용 및 임계값 재설계 |
| 임베디드 | STM32 GPIO 5회 샘플링 디바운싱 로직 설계 (라즈베리파이 신호 안정화) |
| 센서 통합 | HX711 로드셀 무게 측정, 터치 센서 기반 핸들 이탈 감지 연동 |
| 하드웨어 제어 | 부저(Active-Low)·LED·진동 모터 경고 로직 구현 |

## 🔧 폴더 구성

```
Multi-sensor-based-drowsy-driving-prevention-system/
├── raspberrypi/
│   └── blink_detect.py         # 카메라 + MediaPipe 눈 감김 감지, GPIO 출력
├── stm32/
│   └── stm32_weight_safety.c   # 로드셀·터치·라즈베리파이 신호 통합 및 경고 제어
└── README.md
```

## 🖥️ Raspberry Pi 파트 (`blink_detect.py`)

- `cv2.VideoCapture`로 카메라 프레임을 읽고 MediaPipe FaceMesh(`refine_landmarks=True`)로 얼굴 랜드마크 추출
- 좌우 눈의 위/아래 랜드마크(159/145, 386/374) y좌표 차이로 눈뜸 정도 계산
- 임계값(`THRESH`) 미만이 양쪽 눈 모두 `EYE_CLOSE_TIME`초 이상 지속되면 GPIO(BCM17)를 HIGH로 출력, 뜨면 즉시 LOW로 복귀
- 로깅(`logging`)으로 상태 변화와 오류를 콘솔에 기록

## 🔩 STM32 파트 (`stm32_weight_safety.c`)

- `getHX711()` / `weigh()`: HX711 24비트 데이터를 읽어 50회 평균 후 kg 단위로 보정
- `read_rpi_stable()`: 라즈베리파이 GPIO 신호를 5회 읽어 3회 이상 HIGH일 때만 HIGH로 인정하는 디바운싱
- `HAL_TIM_PeriodElapsedCallback()`: TIM2 100ms 주기 인터럽트로 터치 미감지 시간, 로드셀 조건 유지 시간, 시/분/초를 카운팅
- 터치 미감지 5~15초는 LED 경고, 15초 이상은 LED+진동 경고, 라즈베리파이 신호가 안정적으로 HIGH면 부저 ON(Active-Low)

## 🚀 실행 방법

**Raspberry Pi**
```bash
pip install opencv-python mediapipe lgpio
python3 raspberrypi/blink_detect.py
```

**STM32**
`stm32/stm32_weight_safety.c`의 내용을 STM32CubeIDE로 생성한 프로젝트의 `main.c` 해당 USER CODE 영역(전역 변수/함수/메인 루프/인터럽트 콜백)에 나눠서 붙여넣고 빌드합니다.

## 🛠 사용 기술

`Python` `OpenCV` `MediaPipe` `STM32` `HAL` `HX711` `GPIO 디바운싱` `Raspberry Pi`
