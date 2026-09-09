#include <webots/Supervisor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <common/msg/body_task.hpp>
#include <common/msg/head_task.hpp>
#include <common/msg/game_data.hpp>
#include <common/msg/imu_data.hpp>
#include <common/msg/player.hpp>
#include <sys/stat.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "WebotsUtils.hpp"

namespace {
std::string env(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

std::vector<double> parseList(const std::string &text) {
    std::vector<double> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) values.push_back(std::stod(item));
    return values;
}

struct Trial {
    std::string foot;
    double forward;
    double lateral;
    int repeat;
};

struct CameraFrame {
    uint32_t width = 0, height = 0, step = 0;
    std::vector<unsigned char> rgb;
};

bool savePpm(const std::string &path, const CameraFrame &frame) {
    if (!frame.width || !frame.height || frame.step < frame.width * 3 ||
        frame.rgb.size() < static_cast<size_t>(frame.step) * frame.height) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (uint32_t row = 0; row < frame.height; ++row)
        out.write(reinterpret_cast<const char *>(frame.rgb.data() + row * frame.step), frame.width * 3);
    return static_cast<bool>(out);
}
}

int main(int argc, char **argv) {
    std::string judgeName;
    if (!WaitForRobots(judgeName)) return 2;
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("kick_calibration_supervisor");
    auto gamePub = node->create_publisher<common::msg::GameData>("/sensor/game", 5);
    auto bodyPub = node->create_publisher<common::msg::BodyTask>("/red_1/task/body", 5);
    auto headPub = node->create_publisher<common::msg::HeadTask>("/red_1/task/head", 5);
    CameraFrame camera;
    int fall = common::msg::ImuData::FALL_NONE, anyFall = common::msg::ImuData::FALL_NONE;
    auto imageSub = node->create_subscription<sensor_msgs::msg::Image>(
        "/red_1/sensor/image", 2, [&](sensor_msgs::msg::Image::ConstSharedPtr image) {
            if (image->encoding == "rgb8") {
                camera.width = image->width; camera.height = image->height;
                camera.step = image->step; camera.rgb = image->data;
            }
        });
    auto imuSub = node->create_subscription<common::msg::ImuData>(
        "/red_1/sensor/imu", 2,
        [&](common::msg::ImuData::ConstSharedPtr imu) {
            fall = imu->fall;
            if (fall != common::msg::ImuData::FALL_NONE) anyFall = fall;
        });

    std::vector<Trial> trials;
    const auto forwards = parseList(env("CUPCUP_CAL_FORWARD", "0.16,0.20,0.24,0.28"));
    const auto laterals = parseList(env("CUPCUP_CAL_LATERAL", "-0.08,-0.04,0,0.04,0.08"));
    const int repeats = std::max(1, std::stoi(env("CUPCUP_CAL_REPEATS", "1")));
    const std::string feet = env("CUPCUP_CAL_FEET", "left,right");
    for (const std::string foot : {std::string("left"), std::string("right")}) {
        if (feet.find(foot) == std::string::npos) continue;
        for (double forward : forwards) for (double lateral : laterals)
            for (int repeat = 0; repeat < repeats; ++repeat)
                trials.push_back({foot, forward, lateral, repeat});
    }
    const std::string output = env("CUPCUP_CAL_OUTPUT", "/tmp/cupcup-kick-calibration");
    mkdir(output.c_str(), 0755);
    mkdir((output + "/images").c_str(), 0755);
    std::ofstream csv(output + "/trials_truth.csv");
    csv << "trial,foot,forward_m,lateral_m,actual_forward_m,actual_lateral_m,pre_ball_drift_m,"
           "repeat,image,ball_start_x,ball_start_z,"
           "ball_final_x,ball_final_z,forward_displacement_m,lateral_displacement_m,"
           "final_displacement_m,max_displacement_m,robot_final_y,fall\n";

    webots::Supervisor supervisor;
    webots::Node *ball = supervisor.getFromDef("Ball");
    webots::Node *robot = supervisor.getFromDef("red_1");
    webots::Node *blue = supervisor.getFromDef("blue_1");
    if (!ball || !robot || !blue || trials.empty()) return 3;
    constexpr int stepMs = 20;
    const double zeroVelocity[6] = {0, 0, 0, 0, 0, 0};
    const double ballPosition[3] = {0, 0.05, 0};
    const double bluePosition[3] = {-4.4, 0.365, 0};
    const double forwardRotation[4] = {0, 1, 0, 0};
    enum Phase { RESET, READY, OBSERVE, KICK, MEASURE } phase = RESET;
    size_t index = 0;
    double phaseAt = supervisor.getTime(), maxDistance = 0;
    double ballStartX = 0, ballStartZ = 0;
    double actualForward = 0, actualLateral = 0, preBallDrift = 0;
    std::string imageName;

    common::msg::GameData game;
    game.mode = game.MODE_KICK; game.remain_time = 480;
    game.red_players[0].name = "red_1";
    game.blue_players[0].name = "blue_1";
    game.red_players[0].state = common::msg::Player::PLAYER_NORMAL;
    game.blue_players[0].state = common::msg::Player::PLAYER_NORMAL;

    auto beginTrial = [&]() {
        const Trial &trial = trials[index];
        const double robotPosition[3] = {-trial.forward, 0.365, -trial.lateral};
        ball->getField("translation")->setSFVec3f(ballPosition);
        ball->setVelocity(zeroVelocity); ball->resetPhysics();
        robot->getField("translation")->setSFVec3f(robotPosition);
        robot->getField("rotation")->setSFRotation(forwardRotation);
        robot->setVelocity(zeroVelocity); robot->resetPhysics();
        blue->getField("translation")->setSFVec3f(bluePosition);
        blue->setVelocity(zeroVelocity); blue->resetPhysics();
        fall = anyFall = common::msg::ImuData::FALL_NONE;
        phase = RESET; phaseAt = supervisor.getTime(); maxDistance = 0;
    };
    beginTrial();

    while (supervisor.step(stepMs) != -1 && rclcpp::ok() && index < trials.size()) {
        rclcpp::spin_some(node);
        const double now = supervisor.getTime(), elapsed = now - phaseAt;
        common::msg::BodyTask body;
        body.type = body.TASK_WALK; body.count = 0;
        common::msg::HeadTask head; head.yaw = 0; head.pitch = 60;
        game.state = phase == RESET ? game.STATE_INIT :
                     (phase == READY ? game.STATE_READY : game.STATE_PLAY);
        if (phase == KICK) {
            body.type = body.TASK_ACT; body.count = 1;
            body.actname = trials[index].foot + "_kick";
        }
        gamePub->publish(game); bodyPub->publish(body); headPub->publish(head);

        const double *ballNow = ball->getPosition();
        maxDistance = std::max(maxDistance, std::hypot(ballNow[0] - ballStartX, ballNow[2] - ballStartZ));
        if (phase == RESET && elapsed >= 1.5) {
            phase = READY; phaseAt = now;
        } else if (phase == READY && elapsed >= 1.5) {
            phase = OBSERVE; phaseAt = now;
        } else if (phase == OBSERVE && elapsed >= 1.0) {
            std::ostringstream name;
            name << "trial-" << std::setw(4) << std::setfill('0') << index << ".ppm";
            imageName = name.str();
            savePpm(output + "/images/" + imageName, camera);
            const double *robotNow = robot->getPosition();
            ballStartX = ballNow[0]; ballStartZ = ballNow[2]; maxDistance = 0;
            actualForward = ballStartX - robotNow[0];
            actualLateral = ballStartZ - robotNow[2];
            preBallDrift = std::hypot(ballStartX, ballStartZ);
            phase = KICK; phaseAt = now;
        } else if (phase == KICK && elapsed >= 1.2) {
            phase = MEASURE; phaseAt = now;
        } else if (phase == MEASURE && elapsed >= 5.0) {
            const Trial &trial = trials[index];
            const double *robotNow = robot->getPosition();
            const double dx = ballNow[0] - ballStartX, dz = ballNow[2] - ballStartZ;
            csv << index << ',' << trial.foot << ',' << trial.forward << ',' << trial.lateral << ','
                << actualForward << ',' << actualLateral << ',' << preBallDrift << ','
                << trial.repeat << ',' << imageName << ',' << ballStartX << ',' << ballStartZ << ','
                << ballNow[0] << ',' << ballNow[2] << ',' << dx << ',' << dz << ','
                << std::hypot(dx, dz) << ',' << maxDistance << ',' << robotNow[1] << ',' << anyFall << '\n';
            csv.flush();
            RCLCPP_INFO(node->get_logger(), "trial %zu/%zu %s f=%.3f l=%.3f moved=%.3f",
                index + 1, trials.size(), trial.foot.c_str(), trial.forward, trial.lateral,
                std::hypot(dx, dz));
            ++index;
            if (index < trials.size()) beginTrial();
        }
    }
    csv.close();
    RCLCPP_INFO(node->get_logger(), "calibration complete: %zu trials", index);
    supervisor.simulationQuit(0);
    rclcpp::shutdown();
    return index == trials.size() ? 0 : 4;
}
