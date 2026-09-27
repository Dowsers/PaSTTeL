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
  join 1;
  join 2,2;
  join 3,3,3;
  join 4,4,4,4;
  join 5,5,5,5,5;
  join 6,6,6,6,6,6;
  join 7,7,7,7,7,7,7;

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

