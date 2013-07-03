! RUN: %flang -verify < %s

SUBROUTINE SUB(STR, STR2, STR3, STR4, STR5)
  CHARACTER STR*(*)
  CHARACTER STR2*20
  CHARACTER*(*) STR3
  CHARACTER*(10) STR4
  CHARACTER*(*) STR5*(*) ! expected-error {{duplicate character length declaration specifier}}
END

PROGRAM test
  CHARACTER STR*10
  CHARACTER STRING*(STR) ! expected-error {{expected an integer constant expression}}
  CHARACTER STR2*(*) ! expected-error {{use of character length declarator '*' for a local variable 'STR2'}}
  CHARACTER*10 STR3*10 ! expected-error {{duplicate character length declaration specifier}}
END
