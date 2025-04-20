# `mrn_req.

The baseline logic is that for the `/:n` request, such as `/1000`, the code will return a stream of all the numbers that divide 1000, from 1000 down to 1. The code will also sleep in between, making the response long-lived.

The second-order logic is that the connection will go through the proxy.

The third-order logic is that the logic of serving the request will need to be part of different processes (or threads), so that if one of them dies, the next one picks up the job. I am going to model this with threads for now, although ultimately they will need to be different binaries that run on different nodes, of course.

And the fourth-order logic is that the actual implementation of the for-loop that ultimately produces the result will need to be in some async/await-friendly DSL, although for now we just simulate the state machine.

## Howto

To run:

```
pls run
```

Where `pls` is:

```
pip3 install plsbuild
```
