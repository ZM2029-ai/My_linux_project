#include "mqtt_gateway.hpp"

int main(int argc, char **argv)
{
    return runMqttGatewayCommand(argc - 1, argv + 1);
}
