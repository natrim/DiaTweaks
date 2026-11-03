#pragma once
namespace dt {
// TitleBarHeightOption::Collapsed hides native caption controls and their input
// region. Restore only a value we still own; a later app value is its baseline.
struct CaptionLease {
    int original=0;
    bool owned=false;
    template<class Read,class Write> bool Update(bool hidden,Read read,Write write) {
        constexpr int collapsed=2;
        auto current=read();
        if (owned && current!=collapsed) owned=false;
        if (!hidden) {
            if (!owned) return true;
            write(original);
            if (read()!=original) return false;
            owned=false; return true;
        }
        if (current==collapsed) return true;
        original=current; owned=true;
        write(collapsed);
        return read()==collapsed;
    }
};
}
