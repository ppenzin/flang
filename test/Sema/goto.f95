! RUN: %flang -verify < %s
PROGRAM gototest
    INTEGER I

10  I = 0
    GO TO 10

10  I = 10 ! expected-error {{redefinition of statement label '10'}}
    GO TO 20

20  I = 20

    GO TO 30 ! expected-error {{use of undeclared statement label '30'}}
END PROGRAM
