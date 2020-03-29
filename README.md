# Wave-Tags

Code for Wave Tags project. A box that propagates people's messages as fake wi-fi networks.

## Folders

#### /CaptiveCode folder 
contains the code for the Weimon D1 Board that creates the captive portal when your phone connects to the main wi-fi network called "Wave-Tags". It then collects your input from the captive portal and stores a all the data as a string in it's SPIFFS data memory. Everytime any user interacts with the HTML, it sends the new string of messages of to the other board. It uses the htmls in the root folder as webpages to display on people's phones when they connect to the network.

#### /SpammerCode folder 
contains the code for the Weimon D1 Board that propagates the fake beacon packets around the area. It receives a string of messages from the CaptiveCode Board and uses it to propagate a limited number of networks around the area.
