proc foo : (): type {
    if os == Windows {
        return #type u32;
    } else if os == MACOSX {
        return #type u64;
    };
};

type Test : {
    value: u64;
    value2: #run foo();
};

proc main : () {
    var thing: Test;
    thing.value = 1;
    // We would need to know the type of the variable by typechecking time
    // Because we would need to know what type it is when to set the field
    thing.value2 = 7;
};

// If we have to do stuff out of order anyways, is there a reason why we don't
// just define everything?

def foo : proc(): type {
    if os == Windows {
        return #type u32;
    } else if os == MACOSX {
        return #type u64;
    };
};

def Test : struct {
    value: u64;
    value2: #run foo();
};

def main : proc() {
    var thing: Test;
    thing.value = 1;
    // We would need to know the type of the variable by typechecking time
    // Because we would need to know what type it is when to set the field
    thing.value2 = 7;
};

// Stages of compilation

// Tokenizing
// Parsing
// Processing/Typechecking


// Issue is that we need to do typechecking before metaprogramming stuff
// However typechecking may depend on metaprogramming stuff

// Solution: doing it out of order
