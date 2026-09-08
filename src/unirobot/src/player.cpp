#include "topics.hpp"
#include <common/srv/get_color.hpp>

enum RobotColor {
    COLOR_INVALID,
    COLOR_RED,
    COLOR_BLUE
};

RobotColor GetRobotColor(const std::string &name)
{
    if (name.find("red") != std::string::npos) {
        return COLOR_RED;
    } else if (name.find("blue") != std::string::npos) {
        return COLOR_BLUE;
    } else {
        return COLOR_INVALID;
    }
}

int GetRobotId(const std::string &name)
{
    return static_cast<int>(name.back() - '0');
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    std::string robotName = "maxwell";
    std::string friendName = "maxwell";

    std::string tmp = std::string(argv[1]);
    std::string team = tmp.substr(0, tmp.find_last_of('_'));

    auto playerNode = std::make_shared<rclcpp::Node>(tmp + "_player");
    rclcpp::Client<common::srv::GetColor>::SharedPtr client =
        playerNode->create_client<common::srv::GetColor>("gamectrl/get_color");
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

        // ----------------- 可以修改的部分 begin--------------------
        // All competition strategy stays inside the permitted region.
        // Parameters are starting values, to be calibrated using ROS camera images.
        class CupcupPlayer {
        public:
            enum State { WAIT, SEARCH, APPROACH, ORBIT, ALIGN, SETTLE, KICK, VERIFY, RECOVER };
            struct Ball {
                bool valid = false;
                double x = 0, y = 0, radius = 0, score = 0;
            } ball;
            State state = WAIT;
            std::shared_ptr<rclcpp::Node> node;
            rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr cameraSub;
            rclcpp::Subscription<common::msg::ImuData>::SharedPtr imuSub;
            rclcpp::Subscription<common::msg::HeadAngles>::SharedPtr headSub;
            rclcpp::Subscription<common::msg::GameData>::SharedPtr gameSub;
            rclcpp::Subscription<common::msg::Location>::SharedPtr locationSub;
            common::msg::ImuData imu;
            common::msg::HeadAngles head;
            common::msg::Location loc;
            cv::Mat frame;
            cv::dnn::Net ballNet;
            double imageAt = -100, imuAt = -100, headAt = -100, gameAt = -100, locAt = -100;
            double entered = 0, seenAt = -100, uprightAt = 0, lastLog = -100;
            double lastBearing = 0, targetYaw = 0, headYaw = 0, headPitch = 20;
            double goalX, goalYaw, yawSign, yawOffset, kickX, kickY, kickPitch, minScore;
            int gameState = -1, hits = 0, stable = 0, recoveryCount = 0;
            unsigned long sequence = 0, processed = 0;
            bool leftFoot = true, resetRequested = false, fresh = false, measured = false;

            double now() const {
                return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            }
            double clamp(double x, double lo, double hi) const { return std::max(lo, std::min(hi, x)); }
            double wrap(double x) const {
                while (x > 180) x -= 360;
                while (x < -180) x += 360;
                return x;
            }
            const char* name() const {
                switch (state) {
                    case WAIT: return "WAIT"; case SEARCH: return "SEARCH";
                    case APPROACH: return "APPROACH"; case ORBIT: return "ORBIT";
                    case ALIGN: return "ALIGN"; case SETTLE: return "SETTLE";
                    case KICK: return "KICK"; case VERIFY: return "VERIFY";
                    default: return "RECOVER";
                }
            }
            void transition(State next, double t) {
                if (next == state) return;
                state = next; entered = t; stable = 0;
                RCLCPP_INFO(node->get_logger(), "strategy -> %s", name());
            }
            CupcupPlayer(std::shared_ptr<rclcpp::Node> n, const std::string& robot, bool red): node(n) {
                cv::setNumThreads(1);
                const auto modelPath = n->declare_parameter<std::string>("ball_model", "models/bitbots-2026/opencv.onnx");
                if (!modelPath.empty()) {
                    ballNet = cv::dnn::readNetFromONNX(modelPath);
                    RCLCPP_INFO(n->get_logger(), "YOEO ball detector loaded: %s", modelPath.c_str());
                }
                goalX = red ? -4.5 : 4.5;
                goalYaw = n->declare_parameter<double>("attack_yaw", red ? 180.0 : 0.0);
                yawSign = n->declare_parameter<double>("imu_yaw_sign", 1.0);
                yawOffset = n->declare_parameter<double>("imu_yaw_offset", 0.0);
                kickX = clamp(n->declare_parameter<double>("left_kick_x", 0.40), 0.3, 0.49);
                kickY = clamp(n->declare_parameter<double>("kick_y", 0.62), 0.5, 0.85);
                kickPitch = clamp(n->declare_parameter<double>("kick_pitch", 60.0), 35, 70);
                minScore = clamp(n->declare_parameter<double>("ball_min_score", 0.58), 0.3, 0.9);
                // Arrival times are needed: the supplied image/IMU stamps are zero.
                cameraSub = n->create_subscription<sensor_msgs::msg::Image>(robot + "/sensor/image", 2,
                    [this](sensor_msgs::msg::Image::ConstSharedPtr m) {
                        if (!m->width || !m->height || m->width > 4096 || m->height > 4096 ||
                            m->encoding != sensor_msgs::image_encodings::RGB8 || m->step < m->width * 3 ||
                            m->data.size() < static_cast<size_t>(m->step) * m->height) return;
                        frame = cv::Mat(m->height, m->width, CV_8UC3,
                            const_cast<unsigned char*>(m->data.data()), m->step).clone();
                        imageAt = now(); ++sequence;
                    });
                imuSub = n->create_subscription<common::msg::ImuData>(robot + "/sensor/imu", 2,
                    [this](common::msg::ImuData::ConstSharedPtr m) {
                        if (!std::isfinite(m->yaw) || !std::isfinite(m->pitch) || !std::isfinite(m->roll)) return;
                        imu = *m; imuAt = now();
                    });
                headSub = n->create_subscription<common::msg::HeadAngles>(robot + "/sensor/joint/head", 2,
                    [this](common::msg::HeadAngles::ConstSharedPtr m) {
                        if (!std::isfinite(m->yaw) || !std::isfinite(m->pitch)) return;
                        head = *m; headAt = now();
                    });
                gameSub = n->create_subscription<common::msg::GameData>("/sensor/game", 2,
                    [this](common::msg::GameData::ConstSharedPtr m) {
                        if (m->state == m->STATE_INIT && gameState != m->STATE_INIT) resetRequested = true;
                        gameState = m->state; gameAt = now();
                    });
                locationSub = n->create_subscription<common::msg::Location>("/sensor/" + robot + "_location", 2,
                    [this](common::msg::Location::ConstSharedPtr m) {
                        if (!std::isfinite(m->x) || !std::isfinite(m->z)) return;
                        loc = *m; locAt = now();
                    });
                entered = uprightAt = now(); targetYaw = goalYaw;
            }
            Ball detect(const cv::Mat& rgb) {
                if (ballNet.empty()) return detectTraditional(rgb);
                // Reproduce YOEO's centered black padding, nearest resize, RGB /255.
                // The convolution heads are unchanged official weights; decoding is
                // moved here because OpenCV 4.5 cannot execute the exported 5D decoder.
                int side = std::max(rgb.cols, rgb.rows);
                int left = (side-rgb.cols)/2, top = (side-rgb.rows)/2;
                cv::Mat square, resized;
                cv::copyMakeBorder(rgb, square, top, side-rgb.rows-top, left,
                                  side-rgb.cols-left, cv::BORDER_CONSTANT, cv::Scalar());
                cv::resize(square, resized, cv::Size(416,416), 0, 0, cv::INTER_NEAREST);
                ballNet.setInput(cv::dnn::blobFromImage(resized, 1.0/255.0));
                std::vector<cv::Mat> outputs;
                ballNet.forward(outputs, ballNet.getUnconnectedOutLayersNames());
                Ball best; double bestRank = 0;
                auto sigmoid = [](double x) { return 1.0/(1.0+std::exp(-x)); };
                for (const auto& output : outputs) {
                    if (output.dims != 4 || output.size[1] != 7) continue;
                    int height = output.size[2], width = output.size[3];
                    const float* data = output.ptr<float>();
                    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
                        auto value = [&](int channel) { return data[channel*height*width+y*width+x]; };
                        double confidence = sigmoid(value(4))*sigmoid(value(5));
                        if (confidence < 0.30 || value(5) < value(6)) continue;
                        double cx = (sigmoid(value(0))+x)*side/width-left;
                        double cy = (sigmoid(value(1))+y)*side/height-top;
                        double w = std::exp(value(2))*99.99983*side/416;
                        double h = std::exp(value(3))*99.99983*side/416;
                        if (!std::isfinite(w+h) || cx<0 || cy<0 || cx>=rgb.cols || cy>=rgb.rows) continue;
                        double nx=cx/rgb.cols, ny=cy/rgb.rows;
                        double rank=confidence;
                        if (ball.valid && now()-seenAt<0.5 && state!=SEARCH) {
                            double dx=nx-ball.x, dy=ny-ball.y;
                            rank *= 0.65+0.35*std::exp(-20*(dx*dx+dy*dy));
                        }
                        if (rank>bestRank) {
                            bestRank=rank; best.valid=true; best.x=nx; best.y=ny;
                            best.radius=(w+h)/(4*rgb.cols); best.score=confidence;
                        }
                    }
                }
                return best;
            }
            Ball detectTraditional(const cv::Mat& rgb) {
                Ball best;
                cv::Mat small, hsv, white, grass, gray;
                const double scale = 320.0 / rgb.cols;
                cv::resize(rgb, small, cv::Size(), scale, scale, cv::INTER_AREA);
                cv::cvtColor(small, hsv, cv::COLOR_RGB2HSV);
                cv::inRange(hsv, cv::Scalar(0, 0, 105), cv::Scalar(180, 100, 255), white);
                cv::inRange(hsv, cv::Scalar(30, 45, 25), cv::Scalar(95, 255, 255), grass);
                cv::Mat components;
                // Break thin field-line connections before filling the ball's colored patches.
                cv::morphologyEx(white, components, cv::MORPH_OPEN,
                    cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));
                cv::morphologyEx(components, components, cv::MORPH_CLOSE,
                    cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
                std::vector<std::vector<cv::Point>> contours;
                cv::findContours(components, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                std::vector<cv::Vec3f> candidates;
                for (const auto& c : contours) {
                    double area = cv::contourArea(c), perimeter = cv::arcLength(c, true);
                    if (area < 9 || perimeter < 1) continue;
                    cv::Rect box = cv::boundingRect(c);
                    double aspect = double(box.width) / box.height;
                    if (aspect < 0.5 || aspect > 1.9 || 12.56637 * area / (perimeter * perimeter) < 0.38) continue;
                    cv::Point2f center; float radius;
                    cv::minEnclosingCircle(c, center, radius);
                    candidates.emplace_back(center.x, center.y, radius);
                }
                // Circle candidates supplement white contours when colored patches split the ball.
                cv::cvtColor(small, gray, cv::COLOR_RGB2GRAY);
                cv::GaussianBlur(gray, gray, cv::Size(5, 5), 1.2);
                std::vector<cv::Vec3f> circles;
                cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1.5, 12, 100, 20, 3, 90);
                candidates.insert(candidates.end(), circles.begin(), circles.end());
                // Small balls touching a line have too few edge votes for the large-circle threshold.
                circles.clear();
                cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1.0, 8, 100, 11, 4, 18);
                candidates.insert(candidates.end(), circles.begin(), circles.end());
                for (const auto& c : candidates) {
                    double x = c[0], y = c[1], r = c[2];
                    if (r < 3.8 || r > 90 || y < 0.10 * small.rows ||
                        x-r < 1 || y-r < 1 || x+r >= small.cols-1 || y+r >= small.rows-1) continue;
                    if (state != SEARCH && ball.radius > 0 && now()-seenAt < 0.8) {
                        double ratio = (r/small.cols)/ball.radius;
                        if (ratio < 0.65 || ratio > 1.6 ||
                            std::hypot(x/small.cols-ball.x, y/small.rows-ball.y) > 0.20) continue;
                    }
                    int inner = 0, pale = 0, patches = 0, ring = 0, green = 0, upperRing = 0, upperGreen = 0;
                    std::vector<cv::Point> patchPixels;
                    for (int py = std::max(0, int(y-1.6*r)); py < std::min(small.rows, int(y+1.6*r)+1); ++py) {
                        for (int px = std::max(0, int(x-1.6*r)); px < std::min(small.cols, int(x+1.6*r)+1); ++px) {
                            double d = ((px-x)*(px-x)+(py-y)*(py-y))/(r*r);
                            if (d < 0.85) {
                                ++inner; pale += white.at<unsigned char>(py, px) != 0;
                                const auto pixel = hsv.at<cv::Vec3b>(py, px);
                                bool patch = !grass.at<unsigned char>(py, px) && (pixel[2] < 105 || pixel[1] > 100);
                                patches += patch;
                                if (patch) patchPixels.emplace_back(px, py);
                            }
                            if (d > 1.2 && d < 2.5) {
                                bool isGreen = grass.at<unsigned char>(py, px) != 0;
                                ++ring; green += isGreen;
                                if (py < y) { ++upperRing; upperGreen += isGreen; }
                            }
                        }
                    }
                    double whiteness = double(pale) / std::max(1, inner);
                    double surroundings = double(green) / std::max(1, ring);
                    // Clouds above the horizon can form large circles with grass only underneath.
                    if (whiteness < 0.30 || double(patches)/std::max(1, inner) < 0.045 || surroundings < 0.40 ||
                        double(upperGreen)/std::max(1, upperRing) < 0.25) continue;
                    // Ball panels form connected patches; close-up painted grass has isolated speckles.
                    cv::Rect roi(int(x-r), int(y-r), int(2*r)+3, int(2*r)+3);
                    roi &= cv::Rect(0, 0, small.cols, small.rows);
                    cv::Mat patchMask = cv::Mat::zeros(roi.size(), CV_8UC1);
                    for (const auto& pixel : patchPixels)
                        if (roi.contains(pixel)) patchMask.at<unsigned char>(pixel.y-roi.y, pixel.x-roi.x)=255;
                    cv::Mat labels, stats, centroids;
                    int regions = cv::connectedComponentsWithStats(patchMask, labels, stats, centroids);
                    int largest = 0;
                    for (int i=1; i<regions; ++i) largest=std::max(largest, stats.at<int>(i, cv::CC_STAT_AREA));
                    if (largest < std::max(2.0, inner*0.03)) continue;
                    double continuity = 0.5;
                    if (ball.valid) {
                        double dx = x / small.cols - ball.x, dy = y / small.rows - ball.y;
                        continuity = std::exp(-35.0 * (dx*dx + dy*dy));
                    }
                    double score = 0.40 * clamp(whiteness / 0.70, 0, 1) +
                                   0.45 * surroundings + 0.15 * continuity;
                    if (score > best.score) {
                        best.valid = score >= minScore; best.score = score;
                        best.x = x / small.cols; best.y = y / small.rows; best.radius = r / small.cols;
                    }
                }
                return best;
            }
            void tick(common::msg::BodyTask& body, common::msg::HeadTask& ht) {
                double t = now();
                body = common::msg::BodyTask(); body.type = body.TASK_WALK; body.count = 0;
                fresh = processed != sequence;
                measured = false;
                if (resetRequested) {
                    ball = Ball(); hits = stable = recoveryCount = 0; seenAt = -100;
                    headYaw = 0; headPitch = 20; targetYaw = goalYaw;
                    transition(WAIT, t); resetRequested = false; uprightAt = t;
                }
                if (fresh) {
                    Ball next = detect(frame);
                    bool consistent = ball.valid && std::hypot(next.x-ball.x, next.y-ball.y) < 0.18;
                    measured = next.valid;
                    processed = sequence;
                    if (next.valid) {
                        hits = consistent ? std::min(hits+1, 20) : 1;
                        ball = next;
                        seenAt = t;
                        // Horizontal FOV is 1.3613 rad in the supplied camera model.
                        lastBearing = head.yaw + std::atan((0.5-ball.x)*2*std::tan(1.3613/2))*180/3.141592653589793;
                    } else if (t-seenAt > 0.30 || hits < 3) {
                        ball.valid = false; hits = 0;
                    }
                }
                bool sensors = t-imageAt < 0.7 && t-imuAt < 0.7 && t-headAt < 0.7 && t-gameAt < 1.5;
                bool fallen = imu.fall != imu.FALL_NONE;
                if (fallen) uprightAt = t;
                if (gameState != common::msg::GameData::STATE_PLAY || !sensors || fallen || t-uprightAt < 2.0) {
                    transition(WAIT, t); hits = stable = 0;
                } else {
                    if (state == WAIT) transition(SEARCH, t);
                    double age = t-entered;
                    double yaw = wrap(yawSign * imu.yaw + yawOffset);
                    // Coarse legal localization only steers toward the fixed attacking goal.
                    if (t-locAt < 2.0 && std::abs(goalX-loc.x) > 0.7) {
                        double geometric = std::atan2(-loc.z, std::abs(goalX-loc.x))*180/3.141592653589793;
                        double desired = wrap(goalYaw + (goalX < 0 ? geometric : -geometric));
                        targetYaw = wrap(targetYaw + 0.08*wrap(desired-targetYaw));
                    }
                    double error = wrap(targetYaw-yaw);
                    bool visible = ball.valid && t-seenAt < 0.35 && hits >= 3;
                    if (state >= APPROACH && state <= SETTLE && t-seenAt > 1.2) transition(SEARCH, t);
                    if ((state == APPROACH || state == ORBIT || state == ALIGN) && t-entered > (state == APPROACH ? 60.0 : 25.0)) {
                        ++recoveryCount; transition(RECOVER, t);
                    }
                    auto walk = [&](double forward, double side, double turn) {
                        body.count = 1;
                        body.step = clamp(forward, -0.025, 0.04);
                        body.lateral = clamp(side, -0.025, 0.025);
                        body.turn = clamp(turn, -15, 15);
                    };
                    // Track with the head except during the fixed-view kick alignment.
                    if (visible && measured && state >= APPROACH && state <= ORBIT) {
                        // The supplied head topic echoes targets, not encoder feedback.
                        // Apply a small image-error correction to avoid chasing delayed frames.
                        double correction = std::atan((0.5-ball.x)*2*std::tan(1.3613/2))*180/3.141592653589793;
                        headYaw = clamp(head.yaw + clamp(correction*0.25, -3, 3), -65, 65);
                        headPitch = clamp(head.pitch + clamp((ball.y-0.52)*5, -2, 2), 5, kickPitch);
                    }
                    age = t-entered;
                    switch (state) {
                        case SEARCH: {
                            double phase = std::fmod(t-entered, 12.0);
                            headYaw = 60*std::sin(phase*3.141592653589793/3.0);
                            headPitch = phase < 6 ? 18 : 48;
                            if (t-seenAt < 1.2) headYaw = clamp(lastBearing, -65, 65);
                            if (visible) transition(APPROACH, t);
                            else if (phase > 9) walk(0, 0, lastBearing < 0 ? -12 : 12);
                            break;
                        }
                        case APPROACH:
                            if (visible) {
                                double speed = ball.radius < 0.025 ? 0.04 : 0.025;
                                if (std::abs(lastBearing) > 25) speed = 0;
                                walk(speed, 0, lastBearing*0.45);
                                // After a kick the head can still point down while the ball
                                // is far away. Head pitch alone must not trigger foot alignment.
                                if (ball.radius > 0.045) transition(ORBIT, t);
                            }
                            break;
                        case ORBIT:
                            if (visible) {
                                // Turn toward the goal while sidestepping oppositely to keep the ball ahead.
                                double turn = clamp(error*0.35, -10, 10);
                                double side = clamp(lastBearing*0.0008 - turn*0.002, -0.025, 0.025);
                                double forward = ball.radius < 0.04 ? 0.018 : (ball.radius > 0.075 ? -0.015 : 0);
                                walk(forward, side, turn);
                                if (std::abs(error) < 12 && std::abs(lastBearing) < 18) {
                                    leftFoot = lastBearing >= 0; transition(ALIGN, t);
                                }
                            }
                            break;
                        case ALIGN:
                        case SETTLE: {
                            headYaw = 0; headPitch = kickPitch;
                            double desiredX = leftFoot ? kickX : 1-kickX;
                            bool fixedView = (state == SETTLE || t-entered > 0.8) &&
                                std::abs(head.yaw) < 5 && std::abs(head.pitch-kickPitch) < 4;
                            bool linedUp = visible && measured && fixedView && std::abs(error) < 10 &&
                                std::abs(ball.x-desiredX) < 0.035 && std::abs(ball.y-kickY) < 0.045 && ball.radius > 0.045;
                            if (fresh) stable = linedUp ? stable+1 : 0;
                            if (state == SETTLE) {
                                if (!linedUp) transition(ALIGN, t);
                                else if (t-entered > 1.5 && stable >= 5) transition(KICK, t);
                            } else if (visible && fixedView) {
                                if (std::abs(error) > 22) transition(ORBIT, t);
                                else if (stable >= 4) transition(SETTLE, t);
                                else walk((kickY-ball.y)*0.075, (desiredX-ball.x)*0.10, error*0.25);
                            }
                            break;
                        }
                        case KICK:
                            // SETTLE drains the walking queue. Briefly latch one action, then clear it.
                            headYaw = 0; headPitch = kickPitch;
                            if (age < 0.3) {
                                body.type = body.TASK_ACT; body.count = 1;
                                body.actname = leftFoot ? "left_kick" : "right_kick";
                            } else transition(VERIFY, t);
                            break;
                        case VERIFY:
                            headYaw = 0;
                            if (age > 2.5) { hits = 0; transition(SEARCH, t); }
                            break;
                        case RECOVER:
                            if (age < 1.5) walk(-0.02, recoveryCount%2 ? 0.018 : -0.018, 0);
                            else { hits = 0; transition(SEARCH, t); }
                            break;
                        default: break;
                    }
                }
                ht.yaw = headYaw; ht.pitch = headPitch;
                if (!frame.empty()) {
                    cv::Mat debug = frame.clone();
                    if (ball.valid) {
                        cv::circle(debug, cv::Point(ball.x*debug.cols, ball.y*debug.rows),
                            int(ball.radius*debug.cols), cv::Scalar(255, 220, 0), 2);
                    }
                    cv::drawMarker(debug, cv::Point((leftFoot?kickX:1-kickX)*debug.cols, kickY*debug.rows),
                        cv::Scalar(255, 0, 0), cv::MARKER_CROSS, 16, 2);
                    std::string status = std::string(name()) + " ball=" + std::to_string(ball.score).substr(0,4) +
                        " hits=" + std::to_string(hits) + (sensors ? "" : " STALE SENSOR");
                    cv::putText(debug, status, cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255,255,0), 2);
                    frameDebug = debug;
                }
                if (t-lastLog > 2) {
                    RCLCPP_INFO(node->get_logger(), "%s ball=%.2f bearing=%.1f yaw=%.1f goal=%.1f cmd=(%.3f,%.3f,%.1f)",
                        name(), ball.score, lastBearing, imu.yaw, targetYaw, body.step, body.lateral, body.turn);
                    lastLog = t;
                }
            }
            cv::Mat frameDebug;
        };
        static CupcupPlayer strategy(playerNode, robotName, myColor == COLOR_RED);
        rclcpp::spin_some(playerNode);
        strategy.tick(btask, htask);
        if (!strategy.frameDebug.empty()) resImgPublisher->Publish(strategy.frameDebug);
        bodyTaskNode->Publish(btask);
        headTaskNode->Publish(htask);
        loop_rate.sleep();
        // ----------------- 可以修改的部分 end--------------------
    }
    rclcpp::shutdown();
    return 0;
}
