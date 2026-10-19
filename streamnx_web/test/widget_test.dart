import 'package:flutter_test/flutter_test.dart';

import 'package:streamnx_web/main.dart';

void main() {
  testWidgets('App builds without throwing', (tester) async {
    await tester.pumpWidget(const StreamNXApp());
    await tester.pump();
  });
}
