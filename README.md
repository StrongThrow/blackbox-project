# 위험판단 블랙박스

위험판단 블랙박스는 AI를 활용한 블랙박스 시스템으로, 기존 블랙박스에서 알 수 없는 위험을 판단합니다.

<!-- GIF나 이미지 데모 추가 가능: ![데모 GIF](img/demo.gif) -->

# 목차

- [개발 동기](#개발-동기)
- [개발 환경](#개발-환경)
- [설치](#설치)
  - [방법 1: git clone](#방법-1-git-clone)
- [의존성](#의존성)
- [사용법](#사용법)
- [출력 구조](#출력-구조)

### 개발 동기

기존 블랙박스에서는 알 수 없는 위험을 AI로 판단하기 위해 이 프로젝트를 만들었습니다. 이 도구는 다음과 같은 기능을 제공합니다:

- 시스템에서 직접 위험을 판단.
- 사고 전후의 원인 파악에 도움.
  

이 프로젝트는 이러한 요구사항을 모두 충족합니다.

### 개발 환경
---
|개발 환경                                     |                                    |
| -------------------------------------------|----------------------------------- |
| RaspBerryPi5                               |<img width="204" height="192" alt="download" src="https://github.com/user-attachments/assets/1967e8f2-04b4-4823-a7f9-283afba1b94f" />|
| Hailo-8                                    |![download](https://github.com/user-attachments/assets/376ba796-aa6b-4b17-a0df-91064f90510e)|
| Carla                                      |<img width="225" height="225" alt="download (1)" src="https://github.com/user-attachments/assets/756287a7-3cbf-4dbb-87d1-b890e187829f" />|
| Yocto                                      |<img width="363" height="139" alt="download (2)" src="https://github.com/user-attachments/assets/00bfafd9-0f06-4f6b-8f0c-f4cbb2664184" />|


### 설치

---

#### 방법 1: git clone

```bash
$ git clone https://github.com/~
$ cd ~
```

### 의존성 설치
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

### 









