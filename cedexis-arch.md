Cedexis Architecture
====================

The cedexis product allows customers to write custom dns routing
logic in JavaScript to pick between CDN providers at request
time.  The main input data is a "score" of each CDN which
calculated by the cedexis platform based on measurements taken by
cedexis code running in end user browsers.

These measurements are distilled into a score for each CDN from
different locations in the world (more than 5 million).  The
scores plus any custom data are delivered to customer-written
code and a decision is returned.

Edge Realtime Flow
------------------

There are three personas which interact with our system, they
are on the left.  The services marked with `(@)` are written
and owned by my team.

Our customers interact with our portal to configure the system.


```
                    +----------------+
    cedexis +------>| Cedexis Portal +---------v         +----------------+
   customer |       +----------------+     +-------+     | Realtime   (@) |
            |                              |  RMQ  +---->| Distribution   |
            |       +----------------+     +----+--+     +--------------+-+
            +------>| Custom Ingress +-------^  |                       |
                    +----------------+          +-----------+           |
                                                            v           v
                    +------------------+              +-------------+   |
            +------>| Download Service |<------<---+--+ Config  (@) |   |
            |       +------------------+           |  | Service     |   |
            |                                      |  +-------------+   |
            |       +------------------+           |                    v
  community +------>| Session Init (@) |<------<---+                    |
      users |       +------------------+           |                    |
            |                                      |                    |
            |       +--------------------------+   |                    |
            +------>| User Measurements (@)    |<--+                    |
            |       +-----------------------+--+   |                    |
            |                               |      |                    |
            |       +----------------+      |      |                    |
            +------>| Measured Hosts |<--+  +--->--+---->----+          |
                    +----------------+   |  |      |         |          |
                                         |  |      |         v          |
                    +-----------------------+--+   |  +-------------+   |
                    + Synthetic Measurements   |<--+  | Scoring (@) |   |
                    +--------------------------+   |  | Services    |   |
                                                   |  +-------------+   |
                    +-----------------+            |         |          |
 customer's +------>| DNS Service (@) |<-------<---+------<--+--<-------+
      users |       +-----------------+            |         |          |
            |                                      |         v          |
            |       +------------------+           |         |          |
            +------>| HTTP Service (@) |<------<---+------<--+--<-------+
                    +------------------+
```

Edge Log Flow
--------------

All of the edge services my team write (`(@)`, above) deliver
their transaction data in the same way.  Our team also wrote
the loaders that get data into BigQuery as well as the reporting
service used by portal.


```
                           +-------------+
                           | Realtime    |<---+
                           | Services    |    |
                           +-------------+    |
                                              |
               +--------------+     +---------+-------+
     user +--->| Edge Service +---->| Log Service (@) |
               +--------------+     +--------+--------+
                                             |
                       +----------+     +----v---+
                   +---+ Cloud    |<----+ Local  |
                   |   | Storage  |     | Disk   |
                   |   +--+-------+     +--------+
                   |      |
                   |      |  +--------------+    +----------+
                   |      +->| Customer Log +--->| S3 / GCS |
                   |         | Delivery     |    +----------+
                   |         +--------------+
                   |
                   |   +------------+    +--------------+
                   +-->| Notify (@) +--->| BigQuery (@) |
                       | Service    |    | Loader       |
                       +------+-----+    +--+-----------+
                              |             |
                       +------+-----+       |  +-------+------+
                       | Clickhouse |       |  | BigQuery (@) |
                       | Loader (@) |       |  | Aggregator   |
                       +------+-----+       |  +--------------+
                              |             v       |
                       +------+-----+    +--+-------+---+
                       | Clickhouse |    | BigQuery     |
                       +------+-----+    +-------+------+
                              |                  |
                              +------------------+
                                                 |
               +-----------------+     +---------+-------------+
  cedexis +--->| Cedexis Portal  +---->| Reporting Service (@) |
 customer      +-----------------+     +-----------------------+

```

