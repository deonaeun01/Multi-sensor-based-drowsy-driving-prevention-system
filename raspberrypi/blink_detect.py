#!/usr/bin/env python3
import cv2
import mediapipe as mp
import time
import lgpio
import logging
import sys

# ==============================
# 설정
# ==============================
EYE_CLOSE_TIME = 1.0   # 눈 감김 지속 시간이 이 값 이상일 때 GPIO HIGH
GPIO_CHIP = 0           # 첫번째 gpio 칩을 열겠다는 선언
GPIO_PIN = 17           # BCM17 (헤더 11번)

# ==============================
# 로깅 설정
# ==============================
logger = logging.getLogger("blink_detect")
logger.setLevel(logging.INFO)
handler = logging.StreamHandler(sys.stdout)
formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
handler.setFormatter(formatter)
logger.addHandler(handler)

# ==============================
# GPIO 초기화
# ==============================
h = lgpio.gpiochip_open(GPIO_CHIP)
lgpio.gpio_claim_output(h, GPIO_PIN, 0)

# ==============================
# MediaPipe 초기화
# ==============================
mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(refine_landmarks=True)

# ==============================
# 상태 변수
# ==============================
blink_start = None     # 눈 감기기 시작한 시각
is_high = False         # 현재 GPIO가 HIGH인지 상태
last_log_time = 0       # 로그 과다 출력 방지용

# ==============================
# 카메라 열기
# ==============================
cap = cv2.VideoCapture(0)
window_name = "Blink Monitor"

try:
    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            logger.warning("카메라 프레임을 읽지 못했습니다.")
            break

        # 좌우 반전 (거울 모드)
        frame = cv2.flip(frame, 1)

        # RGB로 변환 후 FaceMesh 처리
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = face_mesh.process(rgb_frame)

        h_img, w_img, _ = frame.shape

        # 기본 상태
        eye_state_text = "NO FACE"
        eye_opening = 0.0

        # 얼굴이 감지된 경우
        if results.multi_face_landmarks:
            landmarks = results.multi_face_landmarks[0].landmark

            # ---------- 왼쪽 눈 ----------
            # 위: 159, 아래: 145 - MediaPipe가 고정으로 제공하는 번호
            left_eye_top = landmarks[159]
            left_eye_bottom = landmarks[145]
            left_opening = abs(left_eye_top.y - left_eye_bottom.y)

            # ---------- 오른쪽 눈 ----------
            # 위: 386, 아래: 374 - MediaPipe가 고정으로 제공하는 번호
            right_eye_top = landmarks[386]
            right_eye_bottom = landmarks[374]
            right_opening = abs(right_eye_top.y - right_eye_bottom.y)

            # 픽셀 좌표로 변환해서 눈 위치에 점 찍기 (왼쪽)
            lx_top = int(left_eye_top.x * w_img)
            ly_top = int(left_eye_top.y * h_img)
            lx_bot = int(left_eye_bottom.x * w_img)
            ly_bot = int(left_eye_bottom.y * h_img)
            cv2.circle(frame, (lx_top, ly_top), 2, (0, 255, 0), -1)
            cv2.circle(frame, (lx_bot, ly_bot), 2, (0, 0, 255), -1)

            # 픽셀 좌표로 변환해서 눈 위치에 점 찍기 (오른쪽)
            rx_top = int(right_eye_top.x * w_img)
            ry_top = int(right_eye_top.y * h_img)
            rx_bot = int(right_eye_bottom.x * w_img)
            ry_bot = int(right_eye_bottom.y * h_img)
            cv2.circle(frame, (rx_top, ry_top), 2, (0, 255, 0), -1)
            cv2.circle(frame, (rx_bot, ry_bot), 2, (0, 0, 255), -1)

            # ---------- 양쪽 눈 감김 판단 ----------
            THRESH = 0.010  # 눈 감김 기준 값

            closed_left = left_opening < THRESH
            closed_right = right_opening < THRESH

            # 화면에 표시할 기본 텍스트 (양쪽 EAR 값 표시 느낌)
            base_text = f"L:{left_opening:.4f} R:{right_opening:.4f}"

            if closed_left and closed_right:
                # 양쪽 눈이 모두 감긴 상태
                eye_state_text = "CLOSED " + base_text

                if blink_start is None:
                    blink_start = time.time()
                elif time.time() - blink_start >= EYE_CLOSE_TIME and not is_high:
                    lgpio.gpio_write(h, GPIO_PIN, 1)
                    is_high = True

                    logger.info(
                        f"⚠ 양쪽 눈 감음 {EYE_CLOSE_TIME}초 이상 → GPIO HIGH "
                        f"(L={left_opening:.4f}, R={right_opening:.4f})"
                    )
            else:
                # 한쪽이라도 떠 있으면 OPEN 상태로 취급
                eye_state_text = "OPEN " + base_text

                if is_high:
                    lgpio.gpio_write(h, GPIO_PIN, 0)
                    is_high = False
                    logger.info(
                        f" 눈 뜸 → GPIO LOW "
                        f"(L={left_opening:.4f}, R={right_opening:.4f})"
                    )
                blink_start = None

            # eye_opening을 참고용으로 쓰고 싶으면 최소값/평균값 등으로 설정
            eye_opening = min(left_opening, right_opening)

        # ---------- 터미널 로그 (0.5초마다 한 번씩) ----------
        now = time.time()
        if now - last_log_time > 0.5:
            logger.info(f"상태: {eye_state_text}")
            last_log_time = now

        # ---------- 화면에 상태 텍스트 표시 ----------
        # 눈 상태 텍스트
        cv2.putText(
            frame,
            eye_state_text,
            (10, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (0, 255, 0) if "OPEN" in eye_state_text else (0, 0, 255),
            2,
        )

        # GPIO 상태 텍스트
        gpio_text = "GPIO: HIGH" if is_high else "GPIO: LOW"
        cv2.putText(
            frame,
            gpio_text,
            (10, 60),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.7,
            (255, 255, 255),
            2,
        )

        # ---------- 카메라 화면 표시 ----------
        cv2.imshow(window_name, frame)

        # q 키를 누르면 종료
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

finally:
    # 정리 작업
    cap.release()
    face_mesh.close()
    lgpio.gpiochip_close(h)
    cv2.destroyAllWindows()
    logger.info("프로그램 종료, GPIO 정리 완료")
