Natural version of records/src/validate.moss (reads item.qty / order.items directly from module `orders`).
`moss check src/validate.moss --json` -> ok:true, but `margo build` -> rustc E0616 private field.
Fixed in ../../records by moving predicates into methods on orders.LineItem/Order (`is_valid()`), validate.moss calls those.
