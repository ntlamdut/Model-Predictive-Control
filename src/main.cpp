#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

std::tuple<double, double> mapToVehicleCoordinateTransform(double x, double y, double px, double py, double psi)
{
  double new_x_val = cos(-psi) * (x - px) - sin(-psi) * (y - py);
  double new_y_val = cos(-psi) * (y - py) + sin(-psi) * (x - px);
  return std::make_tuple(new_x_val, new_y_val);
}

// Converts coordinates from map's coordinate space to vehicle's coordinate space
std::tuple<std::vector<double>, std::vector<double>> mapToVehicleCoordinatesTransform(vector<double> x, vector<double> y, double px, double py, double psi)
{
  vector <double> new_x, new_y;
  for (int i = 0; i < x.size(); i++)
  {
    double old_x_val = x[i];
    double old_y_val = y[i];
    
    double new_x_val, new_y_val;
    
    std::tie(new_x_val, new_y_val) = mapToVehicleCoordinateTransform(old_x_val, old_y_val, px, py, psi);
    
    new_x.push_back(new_x_val);
    new_y.push_back(new_y_val);
  }
  return std::make_tuple(new_x, new_y);
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    cout << sdata << endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          vector<double> ptsx = j[1]["ptsx"];
          vector<double> ptsy = j[1]["ptsy"];
          double px = j[1]["x"];
          double py = j[1]["y"];
          double psi = j[1]["psi"];
          double v = j[1]["speed"];

          /*
          * Calculate steeering angle and throttle using MPC.
          *
          * Both are in between [-1, 1].
          *
          */
          std::vector<double> ptsx_car;
          std::vector<double> ptsy_car;
          
          std::tie(ptsx_car, ptsy_car) = mapToVehicleCoordinatesTransform(ptsx, ptsy, px, py, psi);
          
          Eigen::Map<Eigen::VectorXd> e_ptsx(ptsx_car.data(), ptsx_car.size());
          Eigen::Map<Eigen::VectorXd> e_ptsy(ptsy_car.data(), ptsy_car.size());
          auto coeffs = polyfit(e_ptsx, e_ptsy, 3);
          
          double cte = polyeval(coeffs, /*x*/ 0.) - /*y*/ 0.;
          double epsi = -atan(coeffs[1] + 2 * coeffs[2] * /*x*/ 0. + 3 * coeffs[3] * pow(/*x*/ 0., 2));
          
          Eigen::VectorXd state(6);
          state << /*y*/ 0., /*x*/ 0., /*psi*/ 0., v, cte, epsi;
          
          std::vector<double> vars;
          std::vector<double> x_vars;
          std::vector<double> y_vars;
          
          std::tie(vars, x_vars, y_vars) = mpc.Solve(state, coeffs);
          
          double steer_value = vars[6] / -0.436332;
          double throttle_value = vars[7];

          json msgJson;
          msgJson["steering_angle"] = steer_value;
          msgJson["throttle"] = throttle_value;

          //Display the MPC predicted trajectory 
          vector<double> mpc_x_vals = x_vars;
          vector<double> mpc_y_vals = y_vars;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Green line

          msgJson["mpc_x"] = mpc_x_vals;
          msgJson["mpc_y"] = mpc_y_vals;

          //Display the waypoints/reference line
          vector<double> next_x_vals = ptsx_car;
          vector<double> next_y_vals = ptsy_car;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Yellow line

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;


          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          this_thread::sleep_for(chrono::milliseconds(100));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
