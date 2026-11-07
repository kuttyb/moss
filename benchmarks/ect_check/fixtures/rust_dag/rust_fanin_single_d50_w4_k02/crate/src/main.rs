mod dag;

use dag::{root, Score};

pub struct T000 {
    pub v: i64,
}

impl Score for T000 {
    fn score(&self) -> i64 {
        self.v + 8
    }
}

pub struct T001 {
    pub v: i64,
}

impl Score for T001 {
    fn score(&self) -> i64 {
        self.v + 9
    }
}

fn main() {
    let mut total = 0.0;
    total += root(&T000 { v: 1 }) as f64;
    total += root(&T001 { v: 2 }) as f64;
    println!("{}", total);
}
