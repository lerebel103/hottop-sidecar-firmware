# Firmware that doubles the Power of a Hottop Coffee Roaster :grin:

Tired of roasting tiny 200 grams batches that will still run beyond 12 minutes? Well, this firmware is for you: it 
will double the power of your Hottop Coffee Roaster and roast 320g in well under 10 minutes. Plus it connects your
roaster to AWS IoT Core, so you can have something cool to talk about with your friends, win-win!

So that is correct, twice the heating power! Although let me disappoint you now, as you might expect some hardware 
modifications are required also. None of the modifications are sensible or safe, I do not recommend you to do this,
adding more power to this roaster will increase the risk of fire. Additionally, modifying a 240V appliance is just not
something you should ever contemplate doing if you are not qualified. I am not responsible for any damage or injury.

This repository solely exists to share this experiment and results, along with some code that might be useful to others.

## Hardware Modifications
My specific roaster is a Hottop KN-8828B-2K+, although that's not super relevant. You might recognise the popular 
"reverse flow" mod, but perhaps with a twist of its own which I will not get into here.

![Roaster]("media/Roaster.jpg")

You might also spot the suspicious ribbon cable coming out of the back of the roaster. Yes, that is an
ESP-PROG programmer cable that connects to a custom PCB that I designed and made to control a second heater element, I 
occasionally access serial console for convenience, I will get around to removing it at some stage.

So you've guessed it, there is a second heater element and a side-care PCB that controls it. The working principles
are rather straightforward:
* Connect the main control signal harness through a custom PCB
* Intercept the signals, particularly the main heater element duty cycle (it's all simple 5V logic)
* Use a microcontroller (esp32-s3) to then control a second heater element 
* Use a potentiometer so the fraction of heat to the second element can be asjusted by the user as needed
* Drive this second heater element by using a solid state relay (SSR)
* Apply main heater duty cycle scaled by the potentiometer fraction ot the second heater element
* Hard cap max ratio to 70%, let's be reasonable :grinning:
* Add a thermocouple for safety into the main chamber to make sure we limit the risk of fire
* Additionally, this is all connected to AWS IoT Core so, it can tell you if you are about to burn your house down

The PCB is tiny and looks like this:

![PCB]("media/PCB.jpg")

When installed, it tucks in nicely to one of the sides of the roaster walls, whilst the SSRs are mounted on the opposite 
side. If you are wondering, rather than using the power path to the main heater element from the factory board,
I run both heaters from this PCB on their individual SSRs, so I can cut power off for safety. Also note that the SSRs
selected offer the correct range of galvanic isolation, power range and, safety for 240V.

Here are some additional photos of the modifications where you can spot the second heater element and the K-Type 
thermocouple:

![gallery1]("media/gallery1.jpg")
![gallery2]("media/gallery2.png")
![gallery3]("media/gallery3.png")
![gallery4]("media/gallery4.png")


