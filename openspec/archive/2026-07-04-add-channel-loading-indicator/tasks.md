## 1. Add loading state field

- [x] 1.1 Add `bool channel_loading` field to app state struct in source/main.c
- [x] 1.2 Initialize `channel_loading = false` in app initialization

## 2. Set loading state on channel switch

- [x] 2.1 Set `app.channel_loading = true` in `join_channel()` before calling `video_start()`
- [x] 2.2 Verify loading flag NOT set during initial app launch (first `video_start()` call in main)

## 3. Clear loading state when stream ready

- [x] 3.1 Add loading state check in main loop after `video_upload_frame()` call
- [x] 3.2 Clear `app.channel_loading` when `video_has_picture()` returns true
- [x] 3.3 Clear `app.channel_loading` when `app.state` transitions to `STATE_ERROR`

## 4. Display loading indicator

- [x] 4.1 Add loading indicator render code in bottom screen draw function
- [x] 4.2 Display "Loading stream..." text centered when `app.channel_loading` true
- [x] 4.3 Use existing text rendering (citro2d `draw_text()` helper)

## 5. Test scenarios

- [ ] 5.1 Test channel switch from history list shows indicator (USER TEST: requires 3DS hardware)
- [ ] 5.2 Test indicator disappears when first frame loads (USER TEST: requires 3DS hardware)
- [ ] 5.3 Test indicator disappears on offline channel error (USER TEST: requires 3DS hardware)
- [ ] 5.4 Test tab switching during load preserves indicator state (USER TEST: requires 3DS hardware)
- [ ] 5.5 Test rapid channel switches (B before A loads) clear old state correctly (USER TEST: requires 3DS hardware)
