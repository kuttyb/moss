// generated generic DAG: every node is generic over `Score`
pub trait Score {
    fn score(&self) -> i64;
}

pub fn n019_0<T: Score>(x: &T) -> i64 {
    // leaf body
    let k = 1;
    let r = x.score();
    r + k
}

pub fn n019_1<T: Score>(x: &T) -> i64 {
    // leaf body
    let k = 1;
    let r = x.score();
    r + k
}

pub fn n019_2<T: Score>(x: &T) -> i64 {
    // leaf body
    let k = 1;
    let r = x.score();
    r + k
}

pub fn n019_3<T: Score>(x: &T) -> i64 {
    // leaf body
    let k = 1;
    let r = x.score();
    r + k
}

pub fn n018_0<T: Score>(x: &T) -> i64 {
    n019_0(x) + 1
}

pub fn n018_1<T: Score>(x: &T) -> i64 {
    n019_1(x) + 1
}

pub fn n018_2<T: Score>(x: &T) -> i64 {
    n019_2(x) + 1
}

pub fn n018_3<T: Score>(x: &T) -> i64 {
    n019_3(x) + 1
}

pub fn n017_0<T: Score>(x: &T) -> i64 {
    n018_0(x) + 1
}

pub fn n017_1<T: Score>(x: &T) -> i64 {
    n018_1(x) + 1
}

pub fn n017_2<T: Score>(x: &T) -> i64 {
    n018_2(x) + 1
}

pub fn n017_3<T: Score>(x: &T) -> i64 {
    n018_3(x) + 1
}

pub fn n016_0<T: Score>(x: &T) -> i64 {
    n017_0(x) + 1
}

pub fn n016_1<T: Score>(x: &T) -> i64 {
    n017_1(x) + 1
}

pub fn n016_2<T: Score>(x: &T) -> i64 {
    n017_2(x) + 1
}

pub fn n016_3<T: Score>(x: &T) -> i64 {
    n017_3(x) + 1
}

pub fn n015_0<T: Score>(x: &T) -> i64 {
    n016_0(x) + 1
}

pub fn n015_1<T: Score>(x: &T) -> i64 {
    n016_1(x) + 1
}

pub fn n015_2<T: Score>(x: &T) -> i64 {
    n016_2(x) + 1
}

pub fn n015_3<T: Score>(x: &T) -> i64 {
    n016_3(x) + 1
}

pub fn n014_0<T: Score>(x: &T) -> i64 {
    n015_0(x) + 1
}

pub fn n014_1<T: Score>(x: &T) -> i64 {
    n015_1(x) + 1
}

pub fn n014_2<T: Score>(x: &T) -> i64 {
    n015_2(x) + 1
}

pub fn n014_3<T: Score>(x: &T) -> i64 {
    n015_3(x) + 1
}

pub fn n013_0<T: Score>(x: &T) -> i64 {
    n014_0(x) + 1
}

pub fn n013_1<T: Score>(x: &T) -> i64 {
    n014_1(x) + 1
}

pub fn n013_2<T: Score>(x: &T) -> i64 {
    n014_2(x) + 1
}

pub fn n013_3<T: Score>(x: &T) -> i64 {
    n014_3(x) + 1
}

pub fn n012_0<T: Score>(x: &T) -> i64 {
    n013_0(x) + 1
}

pub fn n012_1<T: Score>(x: &T) -> i64 {
    n013_1(x) + 1
}

pub fn n012_2<T: Score>(x: &T) -> i64 {
    n013_2(x) + 1
}

pub fn n012_3<T: Score>(x: &T) -> i64 {
    n013_3(x) + 1
}

pub fn n011_0<T: Score>(x: &T) -> i64 {
    n012_0(x) + 1
}

pub fn n011_1<T: Score>(x: &T) -> i64 {
    n012_1(x) + 1
}

pub fn n011_2<T: Score>(x: &T) -> i64 {
    n012_2(x) + 1
}

pub fn n011_3<T: Score>(x: &T) -> i64 {
    n012_3(x) + 1
}

pub fn n010_0<T: Score>(x: &T) -> i64 {
    n011_0(x) + 1
}

pub fn n010_1<T: Score>(x: &T) -> i64 {
    n011_1(x) + 1
}

pub fn n010_2<T: Score>(x: &T) -> i64 {
    n011_2(x) + 1
}

pub fn n010_3<T: Score>(x: &T) -> i64 {
    n011_3(x) + 1
}

pub fn n009_0<T: Score>(x: &T) -> i64 {
    n010_0(x) + 1
}

pub fn n009_1<T: Score>(x: &T) -> i64 {
    n010_1(x) + 1
}

pub fn n009_2<T: Score>(x: &T) -> i64 {
    n010_2(x) + 1
}

pub fn n009_3<T: Score>(x: &T) -> i64 {
    n010_3(x) + 1
}

pub fn n008_0<T: Score>(x: &T) -> i64 {
    n009_0(x) + 1
}

pub fn n008_1<T: Score>(x: &T) -> i64 {
    n009_1(x) + 1
}

pub fn n008_2<T: Score>(x: &T) -> i64 {
    n009_2(x) + 1
}

pub fn n008_3<T: Score>(x: &T) -> i64 {
    n009_3(x) + 1
}

pub fn n007_0<T: Score>(x: &T) -> i64 {
    n008_0(x) + 1
}

pub fn n007_1<T: Score>(x: &T) -> i64 {
    n008_1(x) + 1
}

pub fn n007_2<T: Score>(x: &T) -> i64 {
    n008_2(x) + 1
}

pub fn n007_3<T: Score>(x: &T) -> i64 {
    n008_3(x) + 1
}

pub fn n006_0<T: Score>(x: &T) -> i64 {
    n007_0(x) + 1
}

pub fn n006_1<T: Score>(x: &T) -> i64 {
    n007_1(x) + 1
}

pub fn n006_2<T: Score>(x: &T) -> i64 {
    n007_2(x) + 1
}

pub fn n006_3<T: Score>(x: &T) -> i64 {
    n007_3(x) + 1
}

pub fn n005_0<T: Score>(x: &T) -> i64 {
    n006_0(x) + 1
}

pub fn n005_1<T: Score>(x: &T) -> i64 {
    n006_1(x) + 1
}

pub fn n005_2<T: Score>(x: &T) -> i64 {
    n006_2(x) + 1
}

pub fn n005_3<T: Score>(x: &T) -> i64 {
    n006_3(x) + 1
}

pub fn n004_0<T: Score>(x: &T) -> i64 {
    n005_0(x) + 1
}

pub fn n004_1<T: Score>(x: &T) -> i64 {
    n005_1(x) + 1
}

pub fn n004_2<T: Score>(x: &T) -> i64 {
    n005_2(x) + 1
}

pub fn n004_3<T: Score>(x: &T) -> i64 {
    n005_3(x) + 1
}

pub fn n003_0<T: Score>(x: &T) -> i64 {
    n004_0(x) + 1
}

pub fn n003_1<T: Score>(x: &T) -> i64 {
    n004_1(x) + 1
}

pub fn n003_2<T: Score>(x: &T) -> i64 {
    n004_2(x) + 1
}

pub fn n003_3<T: Score>(x: &T) -> i64 {
    n004_3(x) + 1
}

pub fn n002_0<T: Score>(x: &T) -> i64 {
    n003_0(x) + 1
}

pub fn n002_1<T: Score>(x: &T) -> i64 {
    n003_1(x) + 1
}

pub fn n002_2<T: Score>(x: &T) -> i64 {
    n003_2(x) + 1
}

pub fn n002_3<T: Score>(x: &T) -> i64 {
    n003_3(x) + 1
}

pub fn n001_0<T: Score>(x: &T) -> i64 {
    n002_0(x) + 1
}

pub fn n001_1<T: Score>(x: &T) -> i64 {
    n002_1(x) + 1
}

pub fn n001_2<T: Score>(x: &T) -> i64 {
    n002_2(x) + 1
}

pub fn n001_3<T: Score>(x: &T) -> i64 {
    n002_3(x) + 1
}

pub fn n000_0<T: Score>(x: &T) -> i64 {
    n001_0(x) + 1
}

pub fn n000_1<T: Score>(x: &T) -> i64 {
    n001_1(x) + 1
}

pub fn n000_2<T: Score>(x: &T) -> i64 {
    n001_2(x) + 1
}

pub fn n000_3<T: Score>(x: &T) -> i64 {
    n001_3(x) + 1
}

pub fn root<T: Score>(x: &T) -> i64 {
    n000_0(x) + n000_1(x) + n000_2(x) + n000_3(x)
}
