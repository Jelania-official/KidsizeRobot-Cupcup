#include "topics.hpp"
#include <common/srv/get_color.hpp>
using namespace std;
using namespace cv;
enum RobotColor {
    COLOR_INVALID,
    COLOR_RED,
    COLOR_BLUE
};
int angle;
int grata;
int grata2;
int grata3;
int grata4;
#define RED 1
#define BLUE -1
RobotColor GetRobotColor(const std::string& name)
{
    if (name.find("red") != std::string::npos) {
        return COLOR_RED;
    } else if (name.find("blue") != std::string::npos) {
        return COLOR_BLUE;
    } else {
        return COLOR_INVALID;
    }
}

int GetRobotId(const std::string& name)
{
    return static_cast<int>(name.back() - '0');
}

int main(int argc, char** argv)
{
    float posx, posy;
    float floatposx, floatposy;
    std::vector<int> Radius;
    bool non = false;
    int previousRadius = 0;
    int currentRadius = 0;

    rclcpp::init(argc, argv);
    std::string robotName = "maxwell";
    std::string friendName = "maxwell";

    std::string tmp = std::string(argv[1]);
    std::string team = tmp.substr(0, tmp.find_last_of('_'));

    auto playerNode = std::make_shared<rclcpp::Node>(tmp + "_player");
    rclcpp::Client<common::srv::GetColor>::SharedPtr client = playerNode->create_client<common::srv::GetColor>("gamectrl/get_color");
    while (!client->wait_for_service(std::chrono::duration<long long>(1))) {
        if (!rclcpp::ok()) {
            RCLCPP_ERROR(playerNode->get_logger(), "Interrupted while waiting for the service. Exiting.");
            return 0;
        }
        RCLCPP_INFO(playerNode->get_logger(), "service not available, waiting again...");
    }
    auto request = std::make_shared<common::srv::GetColor::Request>();
    request.get()->team = team;
    auto result = client->async_send_request(request);
    auto ret = rclcpp::spin_until_future_complete(playerNode, result);
    if (ret == rclcpp::FutureReturnCode::SUCCESS) {
        auto resp = result.get();
        if (resp->color == "invalid") {
            RCLCPP_ERROR(playerNode->get_logger(), "Not supportted team name. Exiting.");
            return 0;
        }
        robotName = resp->color + tmp.substr(tmp.find_last_of('_'));
        friendName = resp->color;
        RCLCPP_INFO(playerNode->get_logger(), "robotName: %s", robotName.c_str());
    } else {
        RCLCPP_ERROR(playerNode->get_logger(), "Exiting.");
        return 0;
    }

    RobotColor myColor = GetRobotColor(robotName);
    int myId = GetRobotId(robotName);
    friendName = friendName + "_" + std::to_string(3 - myId);
    common::msg::BodyTask btask;
    common::msg::HeadTask htask;
    common::msg::GameData gameData;
    common::msg::Location location;

    btask.type = btask.TASK_WALK;
    btask.count = 2;
    btask.step = 0.03;
    htask.yaw = 0.0;
    htask.pitch = 45.0;
    auto bodyTaskNode = std::make_shared<BodyTaskPublisher>(robotName);
    auto headTaskNode = std::make_shared<HeadTaskPublisher>(robotName);
    auto imageSubscriber = std::make_shared<ImageSubscriber>(robotName);
    auto imuSubscriber = std::make_shared<ImuDataSubscriber>(robotName);
    auto headSubscriber = std::make_shared<HeadAngleSubscriber>(robotName);
    auto resImgPublisher = std::make_shared<ResultImagePublisher>(robotName);
    auto gameSubscriber = std::make_shared<GameDataSubscriber>(robotName);
    auto locSubscriber = std::make_shared<LocationSubscriber>(robotName);

    rclcpp::WallRate loop_rate(10.0);
    float initYaw = 0.0;

    while (rclcpp::ok()) {
        rclcpp::spin_some(bodyTaskNode);
        rclcpp::spin_some(headTaskNode);
        rclcpp::spin_some(imageSubscriber);
        rclcpp::spin_some(imuSubscriber);
        rclcpp::spin_some(headSubscriber);
        rclcpp::spin_some(resImgPublisher);
        auto imuData = imuSubscriber->GetData();
        auto image = imageSubscriber->GetImage().clone();
        auto headAngle = headSubscriber->GetData();

        rclcpp::spin_some(gameSubscriber);
        gameData = gameSubscriber->GetData();

        rclcpp::spin_some(locSubscriber); // 更新定位
        location = locSubscriber->GetData();

        // 郑重声明：以下说明仅供参考，实际情况以实际为准
        // 下面提供的是一些可能会用到的相关信息接口说明和示例，可以根据自己的需要去使用，不必要纠结于现有的代码结构
        if (gameData.state == gameData.STATE_INIT) {
            // 每次开球时都会进入这里
            // 例如：RCLCPP_INFO(playerNode->get_logger(), "%d", gameData.blue_score);
            initYaw = imuData.yaw; // 获取init状态时，机器人的朝向，此时的方向即为自己进攻的方向
            grata = 0;
            grata2 = 0;
            grata3 = 0;
            grata4 = 0;
        }
        // 将机器人的当前朝向减去init时的朝向，即可得到机器人相对于初始位置的方向，
        // 这样可以保证自己不管是红色或者蓝色，其yaw都是在面朝对方球门时为0
        // 下面提供这种转换的方法，需要该转换的可以把注释符号去掉
        // imuData.yaw = imuData.yaw - initYaw;

        // IMU数据里的yaw信息是统一的，属于绝对方向，但是红色机器人和蓝色机器人的进球方向是不一样的，
        // 假如红色的进球方向是0度，则蓝色的进球方向就是180度
        // 比赛时使用什么颜色的机器人是现场决定的，所以对于两种颜色的机器人，都需要考虑如何
        // 如果使用了上面的转换方法，则不需要再关心不同颜色机器人的方向问题

        if (myColor == COLOR_RED) {
            // 守门员只会是红色
        } else if (myColor == COLOR_BLUE) {
            // 初赛蓝色机器人守门
            htask.pitch = 30;
            btask.turn = btask.step = 0;
            int lateral = 1;
            RCLCPP_INFO(playerNode->get_logger(), "%d", currentRadius);
            if (currentRadius < 20 || abs(posx - 317) < 40) {
                btask.lateral = 0;
            } else if (posx < 317) {
                btask.lateral = lateral;
            } else {
                btask.lateral = -lateral;
            }
        }
        //////////////////////////////////////////////
        if (myColor == COLOR_BLUE && !image.empty()) {
            cv::Mat midprot;
            cv::Mat talara = image.clone();
            cv::Point pos; // 球心坐标
            cv::Point latasCenter;
            non = false;
            if (Radius.size() >= 1) {
                previousRadius = Radius.at(Radius.size() - 1);
            }

            cv::cvtColor(image, midprot, cv::COLOR_BGR2GRAY);
            GaussianBlur(midprot, midprot, cv::Size(9, 9), 2, 2);

            std::vector<cv::Vec3f> circles;
            if (previousRadius < 40) {
                HoughCircles(midprot, circles, cv::HOUGH_GRADIENT, 1.5, 25, 200, 25, 6, 50);
            } else {
                HoughCircles(midprot, circles, cv::HOUGH_GRADIENT, 1, 25, 200, 25, 30, 70);
            }

            if (circles.size() >= 1) {
                pos.x = cvRound(circles[0][0]);
                pos.y = cvRound(circles[0][1]);
                currentRadius = cvRound(circles[0][2]);
                posx = pos.x;
                posy = pos.y;
                if (sqrt(pow(fabs(floatposx - posx), 2) + pow(fabs(floatposy - posy), 2)) >= 90) {
                    non = false;
                } else {
                    non = true;
                }
                floatposx = posx;
                floatposy = posy;

                cv::putText(image, std::to_string(currentRadius), cv::Point(100, 100), cv::FONT_HERSHEY_SIMPLEX, 0.45, CV_RGB(255, 230, 0), 1.8);
                cv::circle(image, pos, 3, cv::Scalar(0, 255, 0), -1, 2, 0);
                cv::circle(image, pos, currentRadius, cv::Scalar(155, 50, 255), 3, 2, 0);
            } else {
                currentRadius = 0;
            }

            cv::cvtColor(talara, talara, cv::COLOR_BGR2GRAY);
            talara = talara > 200;
            cv::medianBlur(talara, talara, 5);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(talara, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            cv::drawContours(image, contours, -1, cv::Scalar(0, 255, 0), 1, 8);
            int length[50] = { 0 };
            int area[50] = { 0 };
            int maxLenthIndex = 0;
            double dis = 0;

            for (size_t i = 0; i < contours.size(); i++) {
                area[i] = cv::contourArea(contours[i]);
                length[i] = cv::arcLength(contours[i], true);
                if (length[i] > length[maxLenthIndex]) {
                    maxLenthIndex = i;
                }
            }
            double rate = (double)area[maxLenthIndex] / length[maxLenthIndex];
            cv::putText(image, std::to_string(area[maxLenthIndex]), cv::Point(100, 150), cv::FONT_HERSHEY_SIMPLEX, 0.45, CV_RGB(255, 230, 0), 1.8);
            cv::putText(image, std::to_string(length[maxLenthIndex]), cv::Point(100, 200), cv::FONT_HERSHEY_SIMPLEX, 0.45, CV_RGB(255, 230, 0), 1.8);
            cv::putText(image, std::to_string(rate), cv::Point(100, 250), cv::FONT_HERSHEY_SIMPLEX, 0.45, CV_RGB(255, 230, 0), 1.8);
            if (length[maxLenthIndex] > 500) {
                cv::Point2f rect[4];
                cv::RotatedRect box1 = cv::minAreaRect(cv::Mat(contours[maxLenthIndex]));
                latasCenter = box1.center;
                if (non && currentRadius > 40) {
                    dis = cv::norm(latasCenter - pos);
                    if (dis >= 30) {
                        cv::circle(image, cv::Point(box1.center.x, box1.center.y), 5, cv::Scalar(255, 0, 0), -1, 4);
                        box1.points(rect);
                        for (int j = 0; j < 4; j++) {
                            cv::line(image, rect[j], rect[(j + 1) % 4], cv::Scalar(255, 0, 0), 2, 4);
                        }
                    }
                } else {
                    cv::circle(image, cv::Point(box1.center.x, box1.center.y), 5, cv::Scalar(255, 0, 0), -1, 4);
                    box1.points(rect);
                    for (int j = 0; j < 4; j++) {
                        cv::line(image, rect[j], rect[(j + 1) % 4], cv::Scalar(255, 0, 0), 2, 4);
                    }
                }
            }
            resImgPublisher->Publish(image);
        }
        ////////////////////////////

        bodyTaskNode->Publish(btask);
        headTaskNode->Publish(htask);

        loop_rate.sleep();
    }
    return 0;
}
