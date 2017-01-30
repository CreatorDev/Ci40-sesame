
![Creator logo](http://static.creatordev.io/logo.png)

# Sesame gateway application

## Overview
Sesame gateway is a garage door controller application that runs on Ci40 board with Relay Click and Opto Click inserted in click interfaces. It acts as AwaL2MWM client and connects to **Creator Device Server**.

The device server is a LWM2M management server designed to be implemented alongside third party cloud services to integrate M2M capability into an IoT application. The device server exposes two secure interfaces; REST API/HTTPs and LWM2M/CoAP.

Sesame gateway application defines custom IPSO object 13201 to handle door functionality. It also defines two instances of IPSO 3200 object to monitor opened/closed sensors and sends notifications when the status changes.

Sesame gateway application serves the purpose of:
- It acts as Awalwm2m client
- It communicates with device server

Definition of resources: 

| Object Name       | Object ID | Resource Name       | Resource ID | Access Type | Type    |
| :-----------------| :---------| :-------------------| :-----------| :-----------| :-------|
| GarageDoor        | 13201     | DoorTrigger         | 5523        | Execute     | None    |
| GarageDoor        | 13201     | DoorCounter         | 5501        | Read        | Integer |
| GarageDoor        | 13201     | DoorCounterReset    | 5505        | Execute     | None    |
| GarageDoor        | 13201     | DoorDuration        | 5521        | Read        | Float   |
| OptoClick         | 3200      | Digital Input State | 5500        | Read        | Boolean |

Resources used for specific object instances:

| Object ID       | Instance | Name         | Trigger     | Counter | Counter Reset | Duration |
| :---------------| :--------| :------------| :-----------| :-------| :-------------| :--------|
| 13201           | 0        | Door Open    | -           | 5501    | 5505          | 5521     |
| 13201           | 1        | Door Close   | -           | 5501    | 5505          | 5521     |
| 13201           | 2        | Door Trigger | 5523        | 5501    | 5505          | -        |

| Object ID       | Instance | Name          | Digital Input State |
| :---------------| :--------| :-------------| :-------------------|
| 3200            | 0        | Opened Sensor | 5500                |
| 3200            | 1        | Closed Sensor | 5500                |


## Prerequisites
### Hardware
Relay Click should be inserted on Ci40 in Mikrobus Slot 1 and Opto click should be inserted in Slot 2. Below is the image of clicks connected to Ci40 board:
![image](docs/Ci40_HW_setup.jpg)

### App provisioning
App connects to **awa_clientd** running on board through IPC. Awa client needs to be provisioned to device server first. Provisioning can be done using LuCI interface. For this 
[Onboarding Scripts](https://github.com/CreatorDev/ci40-onboarding-scripts) package need to be installed on board.

## Using OpenWrt SDK to build standalone package

Please refer to [Ci40-HelloWorld](https://github.com/CreatorDev/Ci40_helloworld) project for exact build instructions.


## Running Application on Ci40 board
Sesame gateway application can be started from the command line as:

$ sesame_gateway_appd

Output looks something similar to this :

Sesame Gateway Application
```
Sesame Gateway Application ...

------------------------

Waiting for execute command on paths:

- 13201/2/5523

- 13201/0/5505

- 13201/1/5505

Observing Opto Clicks state

Door-Closed state change to 1

Door-Opened state change to 2

```

----

## Contributing

We welcome all contributions to this project and we give credit where it's due. Anything from enhancing functionality to improving documentation and bug reporting - it's all good.

For more details about the Contributor's guidelines, refer to the [contributor guide](https://github.com/CreatorKit/creator-docs/blob/master/ContributorGuide.md).
