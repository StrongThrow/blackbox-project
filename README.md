# AI 위험판단 블랙박스 (AI Risk Detection Blackbox)

AI 기반으로 차량의 위험 상황을 실시간 분석하고, 사고 전후의 맥락을 함께 저장하는 지능형 블랙박스 시스템입니다.
단순한 영상 기록을 넘어, 차량 센서 데이터(CAN)와 AI 모델 분석을 결합해 위험도 기반의 이벤트 감지를 수행합니다.

<!-- GIF나 이미지 데모 추가 가능: ![데모 GIF](img/demo.gif) -->

# 목차
- [프로젝트 개요](#프로젝트-개요)
- [시스템 구성](#시스템-구성)
- [핵심 기술](#핵심-기술)
  - [Carla](#Carla)
  - [CAN 통신](#CAN-통신)
  - [PETR](#PETR)
  - [Yocto](#Yocto)
- [개발 환경](#개발-환경)
- [설치](#설치)
  - [git clone](#git-clone)
  - [의존성](#의존성)
- [사용법](#사용법)
- [출력 구조](#출력-구조)
- [결과 및 시연](#결과-및-시연)

### 프로젝트 개요

기존 블랙박스는 사고가 난 이후의 영상만 기록하기 때문에,
사고 이전의 위험 징후나 차량 상태를 분석하기 어렵다는 한계가 있습니다.

이 프로젝트는 이러한 문제를 해결하기 위해
AI 기반 위험도 판단 및 사고 전후 맥락 분석이 가능한 블랙박스를 구현합니다
  

이 프로젝트는 이러한 요구사항을 모두 충족합니다.

### 시스템 구성
|구성 요소	               |설명                                  |
|--------------------------|--------------------------------------|
|Raspberry Pi 5            |메인 컨트롤러, 영상 수집 및 전처리      |
|Hailo-8	                 |AI 추론 가속기 (PETR 모델 구동)        |
|CAN 모듈                  |차량 상태 데이터 수집 (속도, 브레이크 등)|
|CARLA                     | 시뮬레이터	가상 도로 환경에서의 테스트 |
|Yocto / AGL              | 임베디드 환경 기반 OS 및 런타임 구성    |

### 핵심 기술
------
#### Carla
```bash
추가 바람
```
------
#### CAN 통신
```bash
추가 바람
```
---------
#### PETR
```bash
추가 바람
```
--------
#### Yocto 
```bash
추가 바람
```
--------

### 개발 환경
---
```bash
이미지 추가 바람
```

|항목                  |내용                              |
| ---------------------|----------------------------------|
| 보드                 | RaspBerryPi5                     |
| AI 가속기            | Hailo-8                          |
| 운영체제             | Yocto / AGL 기반 Linux            |
| 언어                 | Python 3.8                       |
| 프레임워크           | Pytorch / ONNX Runtime / Hailo SDK|
| 시뮬레이터           | Carla                             |


### 설치

---

#### git clone

```bash
$ git clone https://github.com/~
$ cd ~
```

#### 의존성 설치
--------

- python3.8
- hailo SDK
- requirements.txt
---
- python3.8 의존성 설치
```bash
$ sudo apt update
$ sudo apt install -y build-essential libssl-dev zlib1g-dev libbz2-dev libreadline-dev libsqlite3-dev curl git libncursesw5-dev xz-utils tk-dev libxml2-dev libxmlsec1-dev libffi-dev liblzma-dev
```

- pyenv 설치
```bash
$ curl https://pyenv.run | bash
```

- vi ~/.bashrc 변경
```bash
export PATH="$HOME/.pyenv/bin:$PATH"
eval "$(pyenv init -)"
eval "$(pyenv virtualenv-init -)"
exec $SHELL
```

- python3.8 설치 & 적용
```bash
$ pyenv install 3.8.0
$ pyenv version
$ pyenv global 3.8.0
```

- requirements 설치
```bash
$ sudo apt update && sudo apt full-upgrade -y
$ sudo raspi-config  # Interface Options > PCIe > Enable
$ sudo reboot
```

```bash
#가상환경 접속 후
$ pip install -r requirements.txt
```

- hailo SDK 설치
```bash
$ sudo dpkg -i hailort_<version>_<architecture>.deb
$ sudo dpkg -i hailort-pcie-driver_<version>_all.deb
```
```bash
$ tar xzf hailo-rt-sdk-4.20.0-rpi.tar.gz
$ cd hailo-rt-sdk-4.20.0-rpi  # 또는 실제 디렉토리
$ ./install.sh
```
```bash
$ python3.8 -m venv hailo_env
$ source hailo_env/bin/activate
$ pip install hailort-4.20.0-cp38-cp38-linux_aarch64.whl
```
- SDK 설치 후 확인
```bash
$ dpkg -l | grep hailo
$ hailortcli fw-control identify
```

- 의존성 오류 시
```bash
$ sudo apt --fix-broken install
```

### 사용법
-----
```bash
$ python vision_server.py
```

### 출력 구조
-------
```bash
트리구조 삽입 바람
```

### 결과 및 시연
----
```bash
추가 바람
```










