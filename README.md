# Streambuf

`StreamBuffer` is a thread-safe and high-performance asyncronous FIFO buffer. No lock in needed when writing to prepared memory or reading from fetched memory. 

## Usage

> The destructor of the `StreamBuffer::owning_view` plays the key role to commit or consume the data. Make sure the view is destroyed if you want to commit or consume the data.


- Create a stream buffer with a maximum size of 1023 elements.

    ```cpp
    StreamBuffer<int, 1024> buffer;
    ```

### Syncronous API

- Prepare and write elements to the buffer.
    `prepare(n)` returns a `StreamBuffer::write_view` object if the buffer has enough space, otherwise it throws `std::out_of_range`. 
    > `StreamBuffer::write_view` is a move-only random access range whose destructor commits the data to the buffer.

    ```cpp
    try {
        auto write_view = buffer.prepare(128);
        for (int i = 0; i < 128; ++i)
            write_view[i] = i;
    } catch (std::out_of_range& e) {
        // The buffer is full.
    }
    ```

- Read elements from the buffer.
    `read(n)` returns a `StreamBuffer::read_view` object if the buffer has enough data, otherwise it throws `std::out_of_range`.
    > `StreamBuffer::read_view` is a move-only random access range whose destructor consumes the data from the buffer.

    ```cpp
    try {
        auto read_view = buffer.read(128);
        for (int i = 0; i < 128; ++i)
            assert(read_view[i] == i);
    } catch (std::out_of_range& e) {
        // The buffer is empty.
    }
    ```

- Read any data from the buffer.
    `read()` returns a `StreamBuffer::read_view` object immediately. The view will be empty, if the buffer is empty.

    ```cpp
    {
        auto read_view = buffer.read();
        if (!read_view.empty()) {
            // The buffer is not empty.
            // The view contains all the data in the buffer.
        }
    }
    ```

### Asyncronous API

- Prepare and write elements to the buffer.
`async_prepare(n)` co_returns a `StreamBuffer::write_view` object if the buffer has enough space, otherwise it suspends the coroutine.

    ```cpp
    {
        auto write_view = co_await buffer.async_prepare(128);
        for (int i = 0; i < 128; ++i)
            write_view[i] = i;
    } 
    // The data will not be committed until the view is destroyed.
    ```

- Read elements from the buffer.
`async_read(n)` co_returns a `StreamBuffer::read_view` object if the buffer has enough data, otherwise it suspends the coroutine. 

    ```cpp
    {
        auto read_view = co_await buffer.async_read(128);
        for (int i = 0; i < 128; ++i)
            assert(read_view[i] == i);
    } 
    // The data will not be consumed until the view is destroyed.
    ```

### Other APIs

The StreamBuffer is also a random access range. You can use the any range algorithms on it but not guaranteed to be thread-safe.


## Dependencies

* Full C++23 support
* Boost asio
