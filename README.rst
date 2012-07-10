=============
pg_retainxlog
=============

``pg_retainxlog`` is designed to be used as an *archive_command*
for PostgreSQL, in situations where there is no traditional
*archive_command* configured. These situations include, but are not
limited to:

* Clusters using ``pg_receivexlog`` to build the log archive in
  realtime (new in PostgreSQL 9.2)
* Clusters using streaming replication without log archiving (in which
  case, it can replace or at least decrease the need for
  *wal_keep_segments*)

The basic functionality of ``pg_retainxlog`` is that when called as
*archive_command* it will compare the current xlog position on the
slave (whether a replication slave or a ``pg_receivexlog`` slave) to
the segment being archived. If the segment being archived is older
than what has been replicated, it does nothing but returns *OK*, which
will cause PostgreSQL to delete/recycle the log segment. If the
segment being archived is newer than what has been replicated, it will
loop until it has been replicated (or until an error occurs, in which
case it will return an error and PostgreSQL will retry the command).
The idea behind this is that the master server will always keep
"enough" WAL segments around for the clients, without wasting space.

Syntax
======

::

 Usage: pg_retainxlog [options] <filename> <connectionstr> 
  -a, --appname    Application name to look for
  -q, --query      Custom query result to look for
  -s, --sleep      Sleep time between attempts (seconds, default=10)
  --verbose        Verbose output
  --help           Show help

filename
--------

This is the name of the file to check for ready-status. This is
normally set using the *%f* parameter in *archive_command*.

connectionstr
-------------

This is the connection string used to connect to the master to get the
replication status. This is a standard *libpq* connection string, and
accepts all parameters that *libpq* does. The connection needs to be
made with a user that has permissions to view the log locations and
application names in *pg_stat_replication*. This typically means
either a superuser or the same user that is used for replication.

appname
-------

By default, ``pg_retainxlog`` will look in *pg_stat_replication*
for any processes with *application_name* set to *pg_streamrecv*. If
you are using ``pg_retainxlog`` together with regular replication,
this needs to be changed to *walreceiver*, or to whatever the
*application_name* has been set to on the replication slave.

Note that if you have more than one replication slave,
``pg_retainxlog`` will return an error if they both ave the same
*application_name*. You must either run them using different names (in
which case you have to decide beforehand which one to care about), or
use a custom query.

query
-----

In many scenarios, the default query will not be flexible enough. For
example, if you have multiple replication slaves, you want
``pg_retainxlog`` to only allow segment removal once it has been
replicated to *all* replication slaves. In this case,
``pg_retainxlog`` lets you specify a custom query that works for
your specific environment. The only requirement is that it must return
a single row, with two fields. These fields must be the oldest xlog
position ok to remove, and a the name of the current xlog file. Any
file *matching this number or newer* will be blocked from removal. The
filename is easiest calculated using the *pg_xlogfile_name()*
function.

If an invalid number of rows *or* an invalid number of fields is
returned, ``pg_retainxlog`` will emit an error and block all
segment renewal.

The default query that is run is::

 SELECT write_location, pg_xlogfile_name(write_location)
   FROM pg_stat_replication
   WHERE application_name='pg_receivexlog'

If you only need to change the *application_name*, use the *-a* or
*--appname* parameter.

sleep
-----

The *sleep* parameter controls how long ``pg_retainxlog`` will
sleep between each query to check status. It will always make a
connection right away and reuse that connection (or if the connection
fails, exit with an error code). The default value is *10 seconds*.


Using with pg_streamrecv
========================

When using in combination with ``pg_receivexlog`` from PostgreSQL 9.2,
the default configuration of ``pg_retainxlog`` should work
fine. You may want to adjust the value of the **-s** parameter to work
well with the status interval chosen in ``pg_receivexlog`` (also the
**-s** parameter in that tool) to avoid unnecessary retry loops.

Simply configure ` `pg_retainxlog`` in your *postgresql.conf* file
with something like::

 archive_mode='on'
 archive_command='/path/to/pg_retainxlog %f "host=ip.of.master"'

Using with replication
======================

The idea behind using ``pg_retainxlog`` with replication is to
remove the need to configure *wal_keep_segments* on the master
server. With ``pg_retainxlog`` properly configured, log files will
be kept around on the master server as long as they are needed,
instead of keeping a fixed number of files around.

If you have a single replication slave, you can run
``pg_retainxlog`` in standard mode by just setting *appname* to
*walreceiver* (or whatever you have configured as *application_name*
on the replication slave). If you are running with multiple
replication slaves, you might want something like this::

 SELECT min(write_location), pg_xlogfile_name(min(write_location))
   FROM pg_stat_replication
   WHERE application_name='walreceiver'

Note that this is a simplified example that **will not work** if your
slaves ever disconnect (which may be exactly the time when you need
this). In order to accommodate for slaves being disconnected, you need
to do something along the line of creating a table holding all your
replication slaves IP addresses and making sure that the query given
to ``pg_retainxlog`` returns either zero rows or a zero WAL
position for any replica that is not connected (thus preventing *all*
recycling until the replica is connected again).


Building
========

Building ``pg_retainxlog`` is a simple *make* command, assuming you
have the ``pg_config`` command in your PATH.::

 make

If not, you can specify that command on the ``make`` commandline like::

 make PG_CONFIG=/some/where/bin/pg_config

``pg_retainxlog`` requires the headers and libraries from
*libpq*. If you are building it on a Linux based platform, that
typically means you need the *-dev* or *-devel* package
(e.g. *libpq-dev* on Debian/Ubuntu or *postgresql*devel* on
RedHat/Fedora/CentOS).
