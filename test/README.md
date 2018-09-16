# Interactive Tests

Currently only the display-profiles are tested here.
Those are tested best manually. The "Features" within this folder
describe the desired behavior!

## Prerequisites

Please, install python "behave".
For Ubuntu the following should be sufficient:
```
sudo apt install python3-behave
```

## Usage
If you have two external monitors you can connect _at the same time_ please call
```
behave --outfile $(git describe)-test-$(date +%F-%T).log
```

If you only have one external monitor call
```
behave --tags onemonitor --outfile $(git describe)-test-$(date +%F-%T).log
```

and follow the instructions.

While testing you can usually:
* continue with <ENTER>
* indicate a problem by typing ```!``` and <ENTER>
* indicate a problem and adding a comment by typing ```!``` followed by you comment and <ENTER>
* just add a hint by writing your message (not starting with ```!```) followed by <ENTER>
