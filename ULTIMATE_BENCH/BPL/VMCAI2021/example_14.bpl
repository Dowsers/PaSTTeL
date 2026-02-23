// #Safe
var x : int;

procedure ULTIMATE.start()
modifies x;
{
  x := 0;

  fork 1 thread1();
  fork 2,2 thread2();
  fork 3,3,3 thread3();
  fork 4,4,4,4 thread4();
  fork 5,5,5,5,5 thread5();
  fork 6,6,6,6,6,6 thread6();
  fork 7,7,7,7,7,7,7 thread7();
  fork 8,8,8,8,8,8,8,8 thread8();
  fork 9,9,9,9,9,9,9,9,9 thread9();
  fork 10,10,10,10,10,10,10,10,10,10 thread10();
  fork 11,11,11,11,11,11,11,11,11,11,11 thread11();
  fork 12,12,12,12,12,12,12,12,12,12,12,12 thread12();
  fork 13,13,13,13,13,13,13,13,13,13,13,13,13 thread13();
  fork 14,14,14,14,14,14,14,14,14,14,14,14,14,14 thread14();
  join 1;
  join 2,2;
  join 3,3,3;
  join 4,4,4,4;
  join 5,5,5,5,5;
  join 6,6,6,6,6,6;
  join 7,7,7,7,7,7,7;
  join 8,8,8,8,8,8,8,8;
  join 9,9,9,9,9,9,9,9,9;
  join 10,10,10,10,10,10,10,10,10,10;
  join 11,11,11,11,11,11,11,11,11,11,11;
  join 12,12,12,12,12,12,12,12,12,12,12,12;
  join 13,13,13,13,13,13,13,13,13,13,13,13,13;
  join 14,14,14,14,14,14,14,14,14,14,14,14,14,14;

  assert x >= 0;
}


procedure thread1()
modifies x;
{
  while (*) {
    x := x + 1;
    x := x * 1;
  }
}


procedure thread2()
modifies x;
{
  while (*) {
    x := x + 2;
    x := x * 2;
  }
}


procedure thread3()
modifies x;
{
  while (*) {
    x := x + 3;
    x := x * 3;
  }
}


procedure thread4()
modifies x;
{
  while (*) {
    x := x + 4;
    x := x * 4;
  }
}


procedure thread5()
modifies x;
{
  while (*) {
    x := x + 5;
    x := x * 5;
  }
}


procedure thread6()
modifies x;
{
  while (*) {
    x := x + 6;
    x := x * 6;
  }
}


procedure thread7()
modifies x;
{
  while (*) {
    x := x + 7;
    x := x * 7;
  }
}


procedure thread8()
modifies x;
{
  while (*) {
    x := x + 8;
    x := x * 8;
  }
}


procedure thread9()
modifies x;
{
  while (*) {
    x := x + 9;
    x := x * 9;
  }
}


procedure thread10()
modifies x;
{
  while (*) {
    x := x + 10;
    x := x * 10;
  }
}


procedure thread11()
modifies x;
{
  while (*) {
    x := x + 11;
    x := x * 11;
  }
}


procedure thread12()
modifies x;
{
  while (*) {
    x := x + 12;
    x := x * 12;
  }
}


procedure thread13()
modifies x;
{
  while (*) {
    x := x + 13;
    x := x * 13;
  }
}


procedure thread14()
modifies x;
{
  while (*) {
    x := x + 14;
    x := x * 14;
  }
}

