#프로젝트 이름

위험판단 블랙박스

##소개

AI블랙박스는 기존 블랙박스에서는 알 수 없는 

##설치 방법

git clone https://github.com/~

##()


1. python3.8 의존성 설치
sudo apt update
sudo apt install -y build-essential libssl-dev zlib1g-dev libbz2-dev libreadline-dev libsqlite3-dev curl git libncursesw5-dev xz-utils tk-dev libxml2-dev libxmlsec1-dev libffi-dev liblzma-dev

2. pyenv 설치
curl https://pyenv.run | bash

3. vi ~/.bashrc 변경
export PATH="$HOME/.pyenv/bin:$PATH"
eval "$(pyenv init -)"
eval "$(pyenv virtualenv-init -)"
exec $SHELL

4. python3.8 설치 & 적용
pyenv install 3.8.0
pyenv version
pyenv global 3.8.0

5. requirements 설치
가상환경 접속 후
pip install -r requirements.txt

6. hailo SDK 설치
sudo dpkg -i hailort_<version>_<architecture>.deb
sudo dpkg -i hailort-pcie-driver_<version>_all.deb
tar -xvf hailort


7. SDK 설치 후 확인
dpkg -l | grep hailo
hailortcli fw-control identify

8. 의존성 오류 시
sudo apt --fix-broken install

##사용법

python vision_server.py
