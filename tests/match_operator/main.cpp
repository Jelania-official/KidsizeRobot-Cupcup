// Evaluation-only operator: uses the ORIGINAL CtrlWindow and referee unchanged.
// Calls its public button slots as a human operator would; never used by player.cpp.
#include "ctrlwindow.hpp"
#include <sensor_msgs/msg/image.hpp>
#include <common/msg/imu_data.hpp>
#include <common/msg/head_angles.hpp>
#include <common/msg/location.hpp>
#include <fstream>
#include <chrono>

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    QApplication app(argc, argv);
    CtrlWindow control("unirobot", "goalkeeper");
    control.show();
    auto observer = std::make_shared<rclcpp::Node>("match_observer");
    common::msg::GameData game;
    common::msg::FieldData field;
    double imageAt = -100, debugAt = -100, fieldAt = -100, imuAt = -100;
    double headAt = -100, locationAt = -100;
    int images = 0, debugImages = 0, fall = 0, attempts = 0;
    auto epoch = std::chrono::steady_clock::now();
    auto now = [&]() {return std::chrono::duration<double>(std::chrono::steady_clock::now()-epoch).count();};
    auto gs = observer->create_subscription<common::msg::GameData>("/sensor/game", 5,
        [&](common::msg::GameData::ConstSharedPtr m) { game = *m; });
    auto fs = observer->create_subscription<common::msg::FieldData>("/sensor/field", 5,
        [&](common::msg::FieldData::ConstSharedPtr m) {field=*m; fieldAt=now();});
    auto is = observer->create_subscription<sensor_msgs::msg::Image>("/red_1/sensor/image", 2,
        [&](sensor_msgs::msg::Image::ConstSharedPtr) {++images; imageAt=now();});
    auto us = observer->create_subscription<common::msg::ImuData>("/red_1/sensor/imu", 2,
        [&](common::msg::ImuData::ConstSharedPtr m) {imuAt=now(); fall=m->fall;});
    auto hs = observer->create_subscription<common::msg::HeadAngles>("/red_1/sensor/joint/head", 2,
        [&](common::msg::HeadAngles::ConstSharedPtr) {headAt=now();});
    auto ls = observer->create_subscription<common::msg::Location>("/sensor/red_1_location", 2,
        [&](common::msg::Location::ConstSharedPtr) {locationAt=now();});
    auto ds = observer->create_subscription<sensor_msgs::msg::Image>("/red_1/result/image", 2,
        [&](sensor_msgs::msg::Image::ConstSharedPtr) {++debugImages; debugAt=now();});
    std::ofstream events("events.csv");
    events << "wall_seconds,event,remaining,red_score,blue_score,ball_state\n";
    auto record = [&](const std::string& what) {
        events << now() << ',' << what << ',' << game.remain_time << ',' << field.red_score << ','
               << field.blue_score << ',' << field.ball_state << std::endl;
        std::cout << what << " remaining=" << game.remain_time << " score=" << field.red_score << ':' << field.blue_score << std::endl;
    };
    int phase=0; double changed=0, lastProgress=0; bool finished=false;
    int shortLimit = 0;
    if (const char* value=std::getenv("CUPCUP_SMOKE_SECONDS")) shortLimit=std::atoi(value);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        rclcpp::spin_some(observer);
        double t=now();
        auto age = [t](double receivedAt) {return receivedAt < 0 ? -1.0 : t-receivedAt;};
        if (finished) return;
        if (t-lastProgress>10) {
            record("progress"); lastProgress=t;
            std::cout << "sensor ages image=" << age(imageAt) << " debug=" << age(debugAt)
                      << " field=" << age(fieldAt) << " imu=" << age(imuAt) << " head=" << age(headAt)
                      << " location=" << age(locationAt) << " fall=" << fall << std::endl;
        }
        bool healthy = images>10 && debugImages>2 && t-imageAt<1 && t-debugAt<1 &&
                       t-fieldAt<1 && t-imuAt<1 && t-headAt<1 && t-locationAt<2 && fall==0;
        if (phase==0 && healthy && t-changed>5) {
            control.OnBtnReadyClicked(); phase=1; changed=t; record("ready");
        } else if (phase==1 && healthy && t-changed>3) {
            control.OnBtnPlayClicked(); phase=2; changed=t; ++attempts; record("play");
        } else if (phase==2 && game.state==game.STATE_PAUSE) {
            record("attempt_end"); phase=3; changed=t;
        } else if (phase==3 && t-changed>1) {
            control.OnBtnInitClicked(); phase=0; changed=t; record("reset");
        }
        bool completed = game.state==game.STATE_END;
        bool smokeEnded = shortLimit>0 && t>shortLimit;
        bool stalled = t>45 && (t-imageAt>15 || t-debugAt>15 || t-fieldAt>15 ||
                                t-imuAt>15 || t-headAt>15 || t-locationAt>15);
        if (completed || smokeEnded || stalled) {
            finished=true;
            if (!completed) control.OnBtnFinishClicked();
            record(completed ? "completed" : (stalled ? "sensor_stall" : "smoke_end"));
            std::ofstream result("result.json");
            result << "{\n  \"completed_8_minutes\": " << (completed?"true":"false")
                   << ",\n  \"sensor_stall\": " << (stalled?"true":"false")
                   << ",\n  \"red_score\": " << field.red_score << ",\n  \"blue_score\": " << field.blue_score
                   << ",\n  \"attempts\": " << attempts << ",\n  \"wall_seconds\": " << t
                   << ",\n  \"controller_remaining\": " << game.remain_time << "\n}\n";
            result.close(); app.quit();
        }
    });
    timer.start(20);
    int code=app.exec();
    rclcpp::shutdown();
    return code;
}
