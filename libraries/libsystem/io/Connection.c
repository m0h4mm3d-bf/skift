
#include <libsystem/Assert.h>
#include <libsystem/__plugs__.h>
#include <libsystem/io/Connection.h>
#include <libsystem/io/Socket.h>

struct Connection
{
    Handle handle;
    struct Socket *socket;
};

Connection *connection_create(Socket *socket, Handle handle)
{
    Connection *connection = __create(Connection);

    connection->handle = handle;
    connection->socket = socket;

    socket_did_connection_open(socket, connection);

    return connection;
}

void connection_close(Connection *connection)
{
    assert(connection != NULL);

    socket_did_connection_close(connection->socket, connection);

    connection->socket = NULL;
    __plug_handle_close(HANDLE(connection));
}

size_t connection_send(Connection *connection, const void *buffer, size_t size)
{
    assert(connection != NULL);
    assert(buffer != NULL);

    return __plug_handle_write(HANDLE(connection), buffer, size);
}

size_t connection_receive(Connection *connection, void *buffer, size_t size)
{
    assert(connection != NULL);
    assert(buffer != NULL);

    return __plug_handle_read(HANDLE(connection), buffer, size);
}
