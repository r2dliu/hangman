runs on Ubuntu

Navigate to the directory in the terminal, and run "make" to create hangman executable. 
Once this is done, run the webserver with "sudo ./hangman <port #> <directory>"

By default, files are served in the "root" folder, so make directory be "root".

example command: "sudo ./hangman 8000 root"

Open the game in browser of choice by typing "localhost:<port #>" into the address bar. Log in with the default user:

username: admin
password: password

Changes in v2:

Made threading useful. Up to 10 (premade) users can login and play games simultaneously

aside from default admin/password user, there are 9 other users which can login with

username: userX
password: passwordX

where X is any int from 1-9

Added graphics of a gallows and a hanging man

Added autofocus on input box

Explanation of game/code logic:

Every word is made up of a combination of the 26 letters in the alphabet. The user must guess the letters in the hidden word, which are revealed as they are guessed correctly. Each letter can only be guessed once, so I used an array of ints of size 26 where each int has a value of 0 or 1; 0 means the letter has not yet been guessed and 1 means the letter has already been guessed. Upon guessing a letter, the game ensures the value in the array is not already 1, or else it ignores the input and resends the current state of the game. After a guess, it scans the hidden word and if it doesn't contain any instances of the letter guessed, the user's guess count is increased, and once it increases to 10, the user loses the game.

Every time the page is refreshed, it rebuilds the current word using a combination of letters and underlines (hidden letters). The generated word replaces the letter with an underline if it has not yet been guessed (0) or uses the actual letter if it has already been guessed. This ensures that if the letter has not been correctly guessed, it will be hidden as an underline.




