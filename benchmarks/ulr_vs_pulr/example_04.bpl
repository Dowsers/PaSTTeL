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
  join 1;
  join 2,2;
  join 3,3,3;
  join 4,4,4,4;

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

