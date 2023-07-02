proc main : (foo: u8) {
    var foo: u64 = 4 - 4;
    syscall3(1, 1, "abcdefg", foo);

    test.foo, bam.bar = 1, 2;

    test(1, 2);
    test(1);

    var foo: test = Foo::Bar;
};
