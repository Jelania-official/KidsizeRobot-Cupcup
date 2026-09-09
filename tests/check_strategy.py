#!/usr/bin/env python3
"""Compile the actual permitted-region strategy into a synthetic-input regression harness.
Run after: colcon build --build-base /tmp/cupcup-build --cmake-args -DBUILD_TESTING=OFF
These tests do not establish Webots performance or replace camera/action calibration.
"""
from pathlib import Path
import os
import sys
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
build = Path(os.environ.get('CUPCUP_BUILD_DIR', str(Path.home()/'.cache/cupcup-build'))) / 'unirobot'
source = (root / 'src/unirobot/src/player.cpp').read_text()
strategy = source[source.index('        class CupcupPlayer {'):source.index('        static CupcupPlayer strategy')]
harness = r'''
    auto node = std::make_shared<rclcpp::Node>("strategy_regression");
    CupcupPlayer p(node, "red_1", true);
    common::msg::BodyTask body;
    common::msg::HeadTask head;
    auto require = [](bool ok, const char* what) {
        if (!ok) { std::cerr << "FAIL: " << what << std::endl; std::exit(1); }
        std::cout << "PASS: " << what << std::endl;
    };
    if (argc > 1) {
        cv::Mat actual = cv::imread(argv[1]);
        cv::cvtColor(actual, actual, cv::COLOR_BGR2RGB);
        auto detected = p.detect(actual);
        std::cout << "ACTUAL valid=" << detected.valid << " x=" << detected.x << " y=" << detected.y << " score=" << detected.score << std::endl;
        require(detected.valid && detected.x < 0.13 && detected.y > 0.62 && detected.y < 0.73,
            "detect ball on field line in captured Webots initial view");
    }
    if (argc > 2) {
        cv::Mat actual = cv::imread(argv[2]);
        cv::cvtColor(actual, actual, cv::COLOR_BGR2RGB);
        auto detected = p.detect(actual);
        std::cout << "LINE valid=" << detected.valid << " x=" << detected.x << " y=" << detected.y << std::endl;
        require(detected.valid && std::abs(detected.x-0.513)<0.04 && std::abs(detected.y-0.505)<0.04,
            "detect ball touching thicker field line while approaching");
    }
    if (argc > 3) {
        cv::Mat actual = cv::imread(argv[3]);
        cv::cvtColor(actual, actual, cv::COLOR_BGR2RGB);
        auto detected = p.detect(actual);
        std::cout << "CLOSE valid=" << detected.valid << " x=" << detected.x << " y=" << detected.y << std::endl;
        require(detected.valid && std::abs(detected.x-0.625)<0.05 && std::abs(detected.y-0.59)<0.05,
            "prefer close ball over speckled white line");
    }
    if (argc > 4) {
        cv::Mat actual = cv::imread(argv[4]);
        cv::cvtColor(actual, actual, cv::COLOR_BGR2RGB);
        auto detected = p.detect(actual);
        std::cout << "KEEPER valid=" << p.keeper.valid << " x=" << p.keeper.x
                  << " score=" << p.keeper.score << std::endl;
        require(detected.valid && p.keeper.valid && p.keeper.score>.45 && p.keeper.y<.4,
            "detect ball and goalkeeper in the same near-goal frame");
    }
    cv::Mat field(480, 640, CV_8UC3, cv::Scalar(35, 135, 40));
    cv::Mat lines = field.clone();
    cv::line(lines, {0,240}, {639,240}, cv::Scalar(255,255,255), 8);
    cv::line(lines, {320,0}, {320,479}, cv::Scalar(255,255,255), 8);
    cv::rectangle(lines, {30,20}, {55,350}, cv::Scalar(255,255,255), -1);
    require(!p.detect(field).valid, "grass is not a ball");
    require(!p.detect(lines).valid, "field lines and upright rectangle are not a ball");
    // Synthetic drawn disks exercise the legacy detector and state machine only.
    // Model accuracy is checked above using captured simulator images.
    p.ballNet = cv::dnn::Net();
    for (int radius : {8, 20, 45, 85}) {
        cv::Mat sample = field.clone();
        cv::circle(sample, {280,280}, radius, cv::Scalar(235,235,235), -1);
        cv::circle(sample, {280,280}, radius/3, cv::Scalar(20,20,20), -1);
        auto b = p.detect(sample);
        require(b.valid && std::abs(b.x-280.0/640)<0.03 && std::abs(b.y-280.0/480)<0.03,
            "locate black/white ball at multiple sizes");
    }
    cv::Mat close = field.clone();
    cv::Point center(int(p.leftKickX*640), int(p.kickY*480));
    cv::circle(close, center, 50, cv::Scalar(235,235,235), -1);
    cv::circle(close, center, 16, cv::Scalar(30,30,180), -1);
    auto feed = [&]() {
        double t = p.now();
        p.imageAt = p.imuAt = p.headAt = p.gameAt = p.locAt = t;
        p.uprightAt = t-5; p.gameState = common::msg::GameData::STATE_PLAY;
        p.imu.yaw = 180; p.imu.fall = 0; p.loc.x=0; p.loc.z=0;
        p.head.pitch = p.kickPitch; p.head.yaw = 0;
        p.frame = close.clone(); ++p.sequence;
    };
    feed(); p.gameState = common::msg::GameData::STATE_PAUSE; p.tick(body,head);
    require(body.count==0 && body.type==body.TASK_WALK, "pause stops commands");
    p.state = CupcupPlayer::ALIGN; p.entered=p.now()-1;
    for (int i=0; i<8; ++i) { feed(); p.tick(body,head); }
    require(p.state==CupcupPlayer::SETTLE && body.count==0, "stable alignment stops before kicking");
    p.entered=p.now()-2;
    for (int i=0; i<5; ++i) { feed(); p.tick(body,head); }
    require(body.type==body.TASK_ACT && body.actname=="left_kick", "aligned ball triggers left kick");
    p.entered=p.now()-0.4; feed(); p.tick(body,head);
    require(body.type==body.TASK_ACT, "kick remains latched across the motion queue window");
    p.entered=p.now()-1.2; feed(); p.tick(body,head);
    require(p.state==CupcupPlayer::VERIFY && body.type==body.TASK_WALK && body.count==0,
        "kick pulse clears and enters observation");
    feed(); p.tick(body,head);
    require(body.type!=body.TASK_ACT, "observation does not repeat kick");
    p.state=CupcupPlayer::APPROACH; feed(); p.imageAt=p.now()-2; p.tick(body,head);
    require(p.state==CupcupPlayer::WAIT && body.count==0, "stale camera stops walking");
    feed(); p.imu.fall=p.imu.FALL_FORWARD; p.tick(body,head);
    require(p.state==CupcupPlayer::WAIT && body.count==0, "fall leaves recovery to motion layer");
    p.state=CupcupPlayer::ORBIT; p.entered=p.now()-26;
    feed(); p.tick(body,head);
    require(p.state==CupcupPlayer::RECOVER && body.step<0, "orbit timeout performs recovery");
    p.resetRequested=true; feed(); p.gameState=common::msg::GameData::STATE_INIT;
    p.tick(body,head);
    require(p.state==CupcupPlayer::WAIT && p.hits==0 && body.count==0, "new round clears strategy");
    require(p.wrap(-358)==2 && p.wrap(358)==-2, "heading error wraps around 180 degrees");
    // Regression from the first successful kick: a low head is not evidence
    // that the ball is at the feet after it has moved away.
    feed();
    p.state=CupcupPlayer::APPROACH; p.entered=p.now(); p.hits=5;
    p.ball=CupcupPlayer::Ball(); p.seenAt=-100;
    p.frame=field.clone();
    cv::circle(p.frame, {320,280}, 10, cv::Scalar(235,235,235), -1);
    cv::circle(p.frame, {320,280}, 3, cv::Scalar(20,20,20), -1);
    for (int i=0; i<4; ++i) { ++p.sequence; p.tick(body,head); }
    require(p.state==CupcupPlayer::APPROACH && body.step>.045,
        "distant ball uses the configured maximum safe forward step");
    p.state=CupcupPlayer::SEARCH; p.entered=p.now(); p.seenAt=-100;
    p.ball=CupcupPlayer::Ball(); p.frame=field.clone(); ++p.sequence;
    p.tick(body,head);
    require(std::abs(head.yaw+55)<1 && std::abs(head.pitch-18)<1,
        "kickoff and distant-ball search start at a held upper-left pose");
    p.state=CupcupPlayer::APPROACH; p.entered=p.now();
    p.ball.valid=true; p.ball.x=.56; p.ball.y=.55; p.ball.radius=.03;
    p.hits=5; p.seenAt=p.now(); p.head.yaw=7; p.head.pitch=25;
    p.headYaw=7; p.headPitch=25; p.measured=true; ++p.sequence; p.frame=close.clone();
    p.tick(body,head);
    require(std::abs(head.yaw-7)<.1 && std::abs(head.pitch-25)<.1,
        "head tracking deadband holds a stable view");
    p.state=CupcupPlayer::APPROACH; p.entered=p.now(); p.targetYaw=p.imu.yaw=180;
    p.keeper={true,.35,.2,.08,.2,.8};
    p.keeperHits=2; p.keeperAt=p.now(); p.shotLaneSelected=false; p.shotYawOffset=0;
    feed(); p.loc.x=-3.0; p.tick(body,head);
    require(p.shotLaneSelected && p.shotYawOffset>0,
        "near-goal approach aims away from a goalkeeper seen on the left");
    p.state=CupcupPlayer::SETTLE; p.entered=p.now()-1; p.stable=3; p.leftFoot=true;
    p.ball.valid=true; p.ball.x=p.leftKickX-.08; p.ball.y=p.kickY+.08;
    p.ball.radius=.07; p.hits=5; p.seenAt=p.now();
    p.head.yaw=0; p.head.pitch=p.kickPitch; p.processed=p.sequence;
    p.tick(body,head);
    require(p.state==CupcupPlayer::KICK,
        "settle hysteresis tolerates small pose jitter and triggers kick");
    p.state=CupcupPlayer::ORBIT; p.entered=p.now(); p.stable=0;
    p.ball.valid=true; p.ball.x=.5; p.ball.y=.55; p.ball.radius=.065;
    p.hits=5; p.seenAt=p.now(); p.head.yaw=20; p.head.pitch=35;
    p.headYaw=20; p.headPitch=35;
    p.frame=field.clone();
    cv::circle(p.frame, {320,264}, 40, cv::Scalar(235,235,235), -1);
    cv::circle(p.frame, {320,264}, 13, cv::Scalar(20,20,20), -1);
    ++p.sequence;
    p.tick(body,head);
    require(p.state==CupcupPlayer::ORBIT && head.yaw<20,
        "orbit recenters the head before fixed-view alignment");
    p.state=CupcupPlayer::ORBIT; p.entered=p.now(); p.targetYaw=p.imu.yaw=180;
    p.head.yaw=0; p.headYaw=0; p.head.pitch=25; p.headPitch=25;
    p.frame=field.clone();
    cv::circle(p.frame, {320,240}, 40, cv::Scalar(235,235,235), -1);
    cv::circle(p.frame, {320,240}, 13, cv::Scalar(20,20,20), -1);
    ++p.sequence; p.hits=5; p.seenAt=p.now();
    p.tick(body,head);
    require(p.state==CupcupPlayer::ORBIT && head.pitch>25,
        "orbit lowers the head before entering calibrated fixed view");
    p.head.yaw=0; p.headYaw=0; p.ball.valid=true; p.ball.radius=.09;
    p.ball.x=.5; p.ball.y=.55; p.hits=5; p.seenAt=p.now();
    p.processed=p.sequence;
    p.tick(body,head);
    require(p.state==CupcupPlayer::ORBIT && body.step<0,
        "orbit backs away from a ball too close for the fixed kick view");
    p.state=CupcupPlayer::ALIGN; p.entered=p.now(); p.seenAt=p.now()-1;
    p.ball.valid=false; p.hits=0; p.processed=p.sequence;
    p.tick(body,head);
    require(p.state==CupcupPlayer::RECOVER && body.step<0,
        "occluded ball at the feet triggers a backward recovery");
    p.entered=p.now()-2; p.frame=field.clone(); ++p.sequence;
    p.tick(body,head); p.tick(body,head);
    require(p.state==CupcupPlayer::SEARCH && std::abs(head.pitch-50)<1,
        "near-ball recovery searches the lower row first");
    rclcpp::shutdown();
    return 0;
'''
flags = []
for line in (build/'CMakeFiles/unirobot.dir/flags.make').read_text().splitlines():
    if line.startswith(('CXX_DEFINES =', 'CXX_INCLUDES =', 'CXX_FLAGS =')):
        flags += shlex.split(line.split('=', 1)[1])
link = shlex.split((build/'CMakeFiles/unirobot.dir/link.txt').read_text())
with tempfile.TemporaryDirectory(prefix='cupcup-regression-') as tmp:
    cpp=Path(tmp)/'check.cpp'; binary=Path(tmp)/'check'
    cpp.write_text('#include "topics.hpp"\n#include <iostream>\nint main(int argc,char**argv){\nrclcpp::init(argc,argv);\n'+strategy+harness+'\n}\n')
    command = [link[0], *flags, '-I'+str(root/'src/unirobot/src'), str(cpp)]
    i=1
    while i<len(link):
        token=link[i]
        if token=='-o': i+=2; continue
        if token.endswith('player.cpp.o'): i+=1; continue
        command.append(token); i+=1
    command += ['-o',str(binary)]
    subprocess.run(command, check=True, cwd=build)
    subprocess.run([str(binary), *(sys.argv[1:] or [str(root/'tests/fixtures/webots_initial_rgb.png'), str(root/'tests/fixtures/webots_ball_on_line.jpg'), str(root/'tests/fixtures/webots_close_ball.jpg'), str(root/'tests/fixtures/webots_goalkeeper.jpg')])], check=True, env={**os.environ, 'ROS_DOMAIN_ID':'91', 'ROS_LOCALHOST_ONLY':'1', 'ROS_LOG_DIR':tmp})
