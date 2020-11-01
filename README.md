# embedded-libraries
Some single-header libraries for embedded development and osdev. (The memory manager mostly works, maybe more to come soon.)

## Design

This set of libraries was designed to hold implementations of algorithms which are generally provided by SDKs but which are typically not portable or easy to customise if you have more specific needs.

An example use case is for Arduino programming, where features like "malloc" are built in to the SDK but may not be usable if you're doing something fancy like multitasking or running complicated sub-modules that need their own record keeping.

## Features

* Memory manager (simple implementations of malloc/free/calloc/realloc-type functions)

More will be added as requirements for other components are identified. (A partial implementation of printf is also present, but not usable yet.)
