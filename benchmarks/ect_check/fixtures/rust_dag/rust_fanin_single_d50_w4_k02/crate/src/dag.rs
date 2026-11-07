// generated generic DAG: every node is generic over `Score`
pub trait Score {
    fn score(&self) -> i64;
}

pub fn c049<T: Score>(x: &T) -> i64 {
    // leaf body
    let k = 1;
    let r = x.score();
    r + k
}

pub fn c048<T: Score>(x: &T) -> i64 {
    c049(x) + 1
}

pub fn c047<T: Score>(x: &T) -> i64 {
    c048(x) + 1
}

pub fn c046<T: Score>(x: &T) -> i64 {
    c047(x) + 1
}

pub fn c045<T: Score>(x: &T) -> i64 {
    c046(x) + 1
}

pub fn c044<T: Score>(x: &T) -> i64 {
    c045(x) + 1
}

pub fn c043<T: Score>(x: &T) -> i64 {
    c044(x) + 1
}

pub fn c042<T: Score>(x: &T) -> i64 {
    c043(x) + 1
}

pub fn c041<T: Score>(x: &T) -> i64 {
    c042(x) + 1
}

pub fn c040<T: Score>(x: &T) -> i64 {
    c041(x) + 1
}

pub fn c039<T: Score>(x: &T) -> i64 {
    c040(x) + 1
}

pub fn c038<T: Score>(x: &T) -> i64 {
    c039(x) + 1
}

pub fn c037<T: Score>(x: &T) -> i64 {
    c038(x) + 1
}

pub fn c036<T: Score>(x: &T) -> i64 {
    c037(x) + 1
}

pub fn c035<T: Score>(x: &T) -> i64 {
    c036(x) + 1
}

pub fn c034<T: Score>(x: &T) -> i64 {
    c035(x) + 1
}

pub fn c033<T: Score>(x: &T) -> i64 {
    c034(x) + 1
}

pub fn c032<T: Score>(x: &T) -> i64 {
    c033(x) + 1
}

pub fn c031<T: Score>(x: &T) -> i64 {
    c032(x) + 1
}

pub fn c030<T: Score>(x: &T) -> i64 {
    c031(x) + 1
}

pub fn c029<T: Score>(x: &T) -> i64 {
    c030(x) + 1
}

pub fn c028<T: Score>(x: &T) -> i64 {
    c029(x) + 1
}

pub fn c027<T: Score>(x: &T) -> i64 {
    c028(x) + 1
}

pub fn c026<T: Score>(x: &T) -> i64 {
    c027(x) + 1
}

pub fn c025<T: Score>(x: &T) -> i64 {
    c026(x) + 1
}

pub fn c024<T: Score>(x: &T) -> i64 {
    c025(x) + 1
}

pub fn c023<T: Score>(x: &T) -> i64 {
    c024(x) + 1
}

pub fn c022<T: Score>(x: &T) -> i64 {
    c023(x) + 1
}

pub fn c021<T: Score>(x: &T) -> i64 {
    c022(x) + 1
}

pub fn c020<T: Score>(x: &T) -> i64 {
    c021(x) + 1
}

pub fn c019<T: Score>(x: &T) -> i64 {
    c020(x) + 1
}

pub fn c018<T: Score>(x: &T) -> i64 {
    c019(x) + 1
}

pub fn c017<T: Score>(x: &T) -> i64 {
    c018(x) + 1
}

pub fn c016<T: Score>(x: &T) -> i64 {
    c017(x) + 1
}

pub fn c015<T: Score>(x: &T) -> i64 {
    c016(x) + 1
}

pub fn c014<T: Score>(x: &T) -> i64 {
    c015(x) + 1
}

pub fn c013<T: Score>(x: &T) -> i64 {
    c014(x) + 1
}

pub fn c012<T: Score>(x: &T) -> i64 {
    c013(x) + 1
}

pub fn c011<T: Score>(x: &T) -> i64 {
    c012(x) + 1
}

pub fn c010<T: Score>(x: &T) -> i64 {
    c011(x) + 1
}

pub fn c009<T: Score>(x: &T) -> i64 {
    c010(x) + 1
}

pub fn c008<T: Score>(x: &T) -> i64 {
    c009(x) + 1
}

pub fn c007<T: Score>(x: &T) -> i64 {
    c008(x) + 1
}

pub fn c006<T: Score>(x: &T) -> i64 {
    c007(x) + 1
}

pub fn c005<T: Score>(x: &T) -> i64 {
    c006(x) + 1
}

pub fn c004<T: Score>(x: &T) -> i64 {
    c005(x) + 1
}

pub fn c003<T: Score>(x: &T) -> i64 {
    c004(x) + 1
}

pub fn c002<T: Score>(x: &T) -> i64 {
    c003(x) + 1
}

pub fn c001<T: Score>(x: &T) -> i64 {
    c002(x) + 1
}

pub fn c000<T: Score>(x: &T) -> i64 {
    c001(x) + 1
}

pub fn e0<T: Score>(x: &T) -> i64 {
    c000(x) + 0
}

pub fn e1<T: Score>(x: &T) -> i64 {
    c000(x) + 1
}

pub fn e2<T: Score>(x: &T) -> i64 {
    c000(x) + 2
}

pub fn e3<T: Score>(x: &T) -> i64 {
    c000(x) + 3
}

pub fn root<T: Score>(x: &T) -> i64 {
    e0(x) + e1(x) + e2(x) + e3(x)
}
