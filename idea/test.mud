mod core : #core;
mod fmt : core::fmt;

const VALUE : 4;

// How do I signify that this is a global vs constant?
global thing : string;

proc main : () {
    thing = "Hello World!";

    var container: Storage::Container[i64];
    container.inner = 4;

    if container.get_value() == VALUE {
        fmt::println(thing);
    }
};

mod Storage : {
    type Container : [A]{
        inner: A;
    };

    #method
    proc get_value : [A](container: *Container[A]): A {
        return container.inner;
    };
};
