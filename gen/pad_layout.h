// made by graphics/make_controllers.ps1 - button positions on pad_xbox.png / pad_ps.png
static const float kPadPicW = 1000.f, kPadPicH = 700.f;
// x, y, radius in picture pixels; order A B X Y LB RB LT RT LS RS Back Start Up Down Left Right
static const float kPadLayout[2][16][3] = {
    { { 700.0f, 370.0f, 27.0f }, { 752.0f, 318.0f, 27.0f }, { 648.0f, 318.0f, 27.0f }, { 700.0f, 266.0f, 27.0f }, { 262.0f, 162.0f, 30.0f }, { 738.0f, 162.0f, 30.0f }, { 278.0f, 106.0f, 32.0f }, { 722.0f, 106.0f, 32.0f }, { 300.0f, 318.0f, 56.0f }, { 610.0f, 432.0f, 56.0f }, { 440.0f, 330.0f, 16.0f }, { 560.0f, 330.0f, 16.0f }, { 392.0f, 404.0f, 15.0f }, { 392.0f, 460.0f, 15.0f }, { 364.0f, 432.0f, 15.0f }, { 420.0f, 432.0f, 15.0f } },
    { { 742.0f, 370.0f, 27.0f }, { 794.0f, 318.0f, 27.0f }, { 690.0f, 318.0f, 27.0f }, { 742.0f, 266.0f, 27.0f }, { 266.0f, 177.0f, 26.0f }, { 734.0f, 177.0f, 26.0f }, { 270.0f, 116.0f, 34.0f }, { 730.0f, 116.0f, 34.0f }, { 396.0f, 440.0f, 56.0f }, { 604.0f, 440.0f, 56.0f }, { 382.0f, 248.0f, 18.0f }, { 618.0f, 248.0f, 18.0f }, { 262.0f, 284.0f, 18.0f }, { 262.0f, 352.0f, 18.0f }, { 228.0f, 318.0f, 18.0f }, { 296.0f, 318.0f, 18.0f } } };
