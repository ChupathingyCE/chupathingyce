import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';

import { colors } from '../theme';

export default function Layout() {
  return (
    <>
      <StatusBar style="light" />
      <Stack
        screenOptions={{
          headerStyle: { backgroundColor: colors.background },
          headerTintColor: colors.text,
          contentStyle: { backgroundColor: colors.background },
        }}>
        <Stack.Screen name="index" options={{ title: 'ChupathingyCE' }} />
      </Stack>
    </>
  );
}
